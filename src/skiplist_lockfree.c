#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdatomic.h>
#include <sched.h>

#define MARK_BIT 1
#define IS_MARKED(p)      ((uintptr_t)(p) & MARK_BIT)
#define GET_UNMARKED(p)   ((Node*)((uintptr_t)(p) & ~MARK_BIT))
#define GET_MARKED(p)     ((Node*)((uintptr_t)(p) | MARK_BIT))

static inline void cpu_relax() {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    __asm__ __volatile__("yield");
#else
    sched_yield();
#endif
}

SkipList* skiplist_create_lockfree(void) {
    SkipList* list = (SkipList*)malloc(sizeof(SkipList));
    if (!list) exit(1);
    
    list->head = create_node(INT_MIN, 0, MAX_LEVEL);
    list->tail = create_node(INT_MAX, 0, MAX_LEVEL);
    
    for (int i = 0; i <= MAX_LEVEL; i++) {
        atomic_store(&list->head->next[i], list->tail);
    }
    
    atomic_init(&list->size, 0);
    list->maxLevel = MAX_LEVEL;
    
    return list;
}

static bool find(SkipList* list, int key, Node** preds, Node** succs) {
    int bottomLevel = 0;
    
retry:
    while (true) {
        Node* pred = list->head;
        
        for (int level = list->maxLevel; level >= bottomLevel; level--) {
            Node* curr = GET_UNMARKED(atomic_load(&pred->next[level]));
            
            while (true) {
                if (curr == NULL) break; 

                Node* succ = atomic_load(&curr->next[level]);
                
                while (IS_MARKED(succ)) {
                    Node* unmarked_succ = GET_UNMARKED(succ);
                    if (!atomic_compare_exchange_strong(&pred->next[level], &curr, unmarked_succ)) {
                        cpu_relax();
                        goto retry;
                    }
                    curr = unmarked_succ;
                    if (curr == NULL) break;
                    succ = atomic_load(&curr->next[level]);
                }
                
                if (curr == NULL) break;
                
                if (curr != list->tail && curr->key < key) {
                    pred = curr;
                    curr = GET_UNMARKED(succ);
                } else {
                    break; 
                }
            }
            
            preds[level] = pred;
            succs[level] = curr;
        }
        
        return (succs[bottomLevel] != list->tail && succs[bottomLevel]->key == key);
    }
}

bool skiplist_insert_lockfree(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    while (true) {
        if (find(list, key, preds, succs)) {
            return false; 
        }
        
        int topLevel = random_level(); 
        Node* newNode = create_node(key, value, topLevel);
        
        for (int i = 0; i <= topLevel; i++) {
            atomic_store(&newNode->next[i], succs[i]);
        }
        
        Node* pred = preds[0];
        Node* succ = succs[0];
        
        if (!atomic_compare_exchange_strong(&pred->next[0], &succ, newNode)) {
            omp_destroy_lock(&newNode->lock);
            free(newNode);
            cpu_relax(); 
            continue; 
        }
        
        atomic_fetch_add(&list->size, 1);
        
        for (int i = 1; i <= topLevel; i++) {
            while (true) {
                pred = preds[i];
                succ = succs[i];
                
                if (atomic_compare_exchange_strong(&pred->next[i], &succ, newNode)) {
                    break; 
                }
                
                find(list, key, preds, succs);
                
                if (IS_MARKED(atomic_load(&newNode->next[0]))) {
                    return true; 
                }
                
                atomic_store(&newNode->next[i], succs[i]);
                cpu_relax(); 
            }
        }
        
        atomic_store(&newNode->fully_linked, true);
        return true;
    }
}

bool skiplist_delete_lockfree(SkipList* list, int key) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    Node* victim;
    
    while (true) {
        if (!find(list, key, preds, succs)) {
            return false; 
        }
        
        victim = succs[0];
        
        for (int i = victim->topLevel; i >= 0; i--) {
            Node* succ = atomic_load(&victim->next[i]);
            
            if (IS_MARKED(succ)) {
                if (i == 0) return false; 
                continue;
            }
            
            Node* marked_succ = GET_MARKED(succ);
            if (!atomic_compare_exchange_strong(&victim->next[i], &succ, marked_succ)) {
                if (i == 0) {
                    cpu_relax(); 
                    goto retry_delete; 
                }
            }
        }
        
        find(list, key, preds, succs);
        atomic_fetch_sub(&list->size, 1);
        return true;
        
        retry_delete:;
    }
}

bool skiplist_contains_lockfree(SkipList* list, int key) {
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = GET_UNMARKED(atomic_load(&pred->next[level]));
        
        while (true) {
            if (curr == NULL || curr == list->tail) break;
            Node* succ = atomic_load(&curr->next[level]);
            
            while (IS_MARKED(succ)) {
                curr = GET_UNMARKED(succ);
                if (curr == NULL || curr == list->tail) goto next_level;
                succ = atomic_load(&curr->next[level]);
            }
            
            if (curr->key < key) {
                pred = curr;
                curr = GET_UNMARKED(succ);
            } else {
                break;
            }
        }
        next_level:;
    }
    
    Node* curr = GET_UNMARKED(atomic_load(&pred->next[0]));
    
    return (curr != list->tail && 
            curr->key == key && 
            !IS_MARKED(atomic_load(&curr->next[0])));
}

void skiplist_destroy_lockfree(SkipList* list) {
    Node* curr = list->head;
    while (curr) {
        Node* next = GET_UNMARKED(atomic_load(&curr->next[0]));
        omp_destroy_lock(&curr->lock);
        free(curr);
        curr = next;
    }
    free(list);
}