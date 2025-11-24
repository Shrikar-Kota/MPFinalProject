#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdint.h>

SkipList* skiplist_create_fine(void) {
    SkipList* list = (SkipList*)malloc(sizeof(SkipList));
    list->head = create_node(INT_MIN, 0, MAX_LEVEL);
    list->tail = create_node(INT_MAX, 0, MAX_LEVEL);
    list->maxLevel = MAX_LEVEL;
    atomic_init(&list->size, 0);
    for (int i = 0; i <= MAX_LEVEL; i++) {
        atomic_store(&list->head->next[i], list->tail);
        atomic_store(&list->tail->next[i], NULL);
    }
    return list;
}

// Helper to lock nodes in address order (prevents deadlock)
static void lock_nodes_ordered(Node** nodes, int count, Node** locked, int* locked_count) {
    Node* unique[MAX_LEVEL + 2];
    int unique_cnt = 0;
    
    for (int i = 0; i < count; i++) {
        if (nodes[i] == NULL) continue;
        bool found = false;
        for (int j = 0; j < unique_cnt; j++) {
            if (unique[j] == nodes[i]) { found = true; break; }
        }
        if (!found) unique[unique_cnt++] = nodes[i];
    }
    
    // Simple bubble sort by address
    for (int i = 0; i < unique_cnt - 1; i++) {
        for (int j = i + 1; j < unique_cnt; j++) {
            if ((uintptr_t)unique[i] > (uintptr_t)unique[j]) {
                Node* temp = unique[i];
                unique[i] = unique[j];
                unique[j] = temp;
            }
        }
    }
    
    for (int i = 0; i < unique_cnt; i++) {
        omp_set_lock(&unique[i]->lock);
        locked[i] = unique[i];
    }
    *locked_count = unique_cnt;
}

static void unlock_nodes_ordered(Node** locked, int count) {
    for (int i = 0; i < count; i++) {
        if (locked[i] != NULL) omp_unset_lock(&locked[i]->lock);
    }
}

bool skiplist_insert_fine(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    Node* locked[MAX_LEVEL + 1];
    int locked_count = 0;
    
    while (true) {
        // Phase 1: Optimistic Traversal (No locks)
        Node* pred = list->head;
        for (int level = list->maxLevel; level >= 0; level--) {
            Node* curr = UNMARK_PTR(atomic_load(&pred->next[level]));
            while (curr != list->tail && curr->key < key) {
                pred = curr;
                curr = UNMARK_PTR(atomic_load(&pred->next[level]));
            }
            preds[level] = pred;
            succs[level] = curr;
        }
        
        if (succs[0] != list->tail && succs[0]->key == key) return false;
        
        int topLevel = random_level();
        Node* newNode = create_node(key, value, topLevel);
        for (int level = 0; level <= topLevel; level++) {
            atomic_store(&newNode->next[level], succs[level]);
        }
        
        lock_nodes_ordered(preds, topLevel + 1, locked, &locked_count);
        
        // Validation
        bool valid = true;
        for (int level = 0; level <= topLevel; level++) {
            Node* curr = UNMARK_PTR(atomic_load(&preds[level]->next[level]));
            if (curr != succs[level] || (curr != list->tail && curr->key == key)) {
                valid = false; break;
            }
        }
        
        if (!valid) {
            unlock_nodes_ordered(locked, locked_count);
            // Safe to free newNode here because it was never linked to the list
            omp_destroy_lock(&newNode->lock);
            free(newNode);
            continue;
        }
        
        for (int level = 0; level <= topLevel; level++) {
            atomic_store(&preds[level]->next[level], newNode);
        }
        atomic_store(&newNode->fully_linked, true);
        
        unlock_nodes_ordered(locked, locked_count);
        atomic_fetch_add(&list->size, 1);
        return true;
    }
}

bool skiplist_delete_fine(SkipList* list, int key) {
    Node* preds[MAX_LEVEL + 1];
    Node* victim = NULL;
    Node* to_lock[MAX_LEVEL + 2];
    Node* locked[MAX_LEVEL + 2];
    int locked_count = 0;
    
    while (true) {
        Node* pred = list->head;
        for (int level = list->maxLevel; level >= 0; level--) {
            Node* curr = UNMARK_PTR(atomic_load(&pred->next[level]));
            while (curr != list->tail && curr->key < key) {
                pred = curr;
                curr = UNMARK_PTR(atomic_load(&pred->next[level]));
            }
            preds[level] = pred;
            if (level == 0) {
                if (curr != list->tail && curr->key == key) victim = curr;
                else return false;
            }
        }
        
        for (int i = 0; i <= victim->topLevel; i++) to_lock[i] = preds[i];
        to_lock[victim->topLevel + 1] = victim;
        
        lock_nodes_ordered(to_lock, victim->topLevel + 2, locked, &locked_count);
        
        bool valid = true;
        for (int level = 0; level <= victim->topLevel; level++) {
            Node* curr = UNMARK_PTR(atomic_load(&preds[level]->next[level]));
            if (curr != victim) { valid = false; break; }
        }
        
        if (!valid) {
            unlock_nodes_ordered(locked, locked_count);
            continue;
        }
        
        for (int level = 0; level <= victim->topLevel; level++) {
            Node* succ = UNMARK_PTR(atomic_load(&victim->next[level]));
            atomic_store(&preds[level]->next[level], succ);
        }
        
        unlock_nodes_ordered(locked, locked_count);
        atomic_fetch_sub(&list->size, 1);
        
        // CRITICAL FIX: Do NOT free(victim) immediately.
        // Other threads might be optimistically traversing it right now.
        // omp_destroy_lock(&victim->lock);
        // free(victim); <--- REMOVED TO PREVENT CRASH
        
        return true;
    }
}

bool skiplist_contains_fine(SkipList* list, int key) {
    Node* pred = list->head;
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = UNMARK_PTR(atomic_load(&pred->next[level]));
        while (curr != list->tail && curr->key < key) {
            pred = curr;
            curr = UNMARK_PTR(atomic_load(&pred->next[level]));
        }
        if (level == 0) return (curr != list->tail && curr->key == key);
    }
    return false;
}

void skiplist_destroy_fine(SkipList* list) {
    Node* curr = list->head;
    while (curr != NULL) {
        Node* next = UNMARK_PTR(atomic_load(&curr->next[0]));
        omp_destroy_lock(&curr->lock);
        free(curr);
        curr = next;
    }
    free(list);
}