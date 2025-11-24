#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

SkipList* skiplist_create_coarse(void) {
    SkipList* list = (SkipList*)malloc(sizeof(SkipList));
    list->head = create_node(INT_MIN, 0, MAX_LEVEL);
    list->tail = create_node(INT_MAX, 0, MAX_LEVEL);
    list->maxLevel = MAX_LEVEL;
    atomic_init(&list->size, 0);
    for (int i = 0; i <= MAX_LEVEL; i++) {
        atomic_store(&list->head->next[i], list->tail);
        atomic_store(&list->tail->next[i], NULL);
    }
    omp_init_lock(&list->lock);
    return list;
}

bool skiplist_insert_coarse(SkipList* list, int key, int value) {
    omp_set_lock(&list->lock);
    
    Node* preds[MAX_LEVEL + 1];
    Node* curr = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* next = UNMARK_PTR(atomic_load(&curr->next[level]));
        while (next != list->tail && next->key < key) {
            curr = next;
            next = UNMARK_PTR(atomic_load(&curr->next[level]));
        }
        preds[level] = curr;
        if (level == 0 && next != list->tail && next->key == key) {
            omp_unset_lock(&list->lock);
            return false;
        }
    }
    
    int newLevel = random_level();
    Node* newNode = create_node(key, value, newLevel);
    for (int i = 0; i <= newLevel; i++) {
        Node* succ = UNMARK_PTR(atomic_load(&preds[i]->next[i]));
        atomic_store(&newNode->next[i], succ);
        atomic_store(&preds[i]->next[i], newNode);
    }
    
    atomic_fetch_add(&list->size, 1);
    omp_unset_lock(&list->lock);
    return true;
}

bool skiplist_delete_coarse(SkipList* list, int key) {
    omp_set_lock(&list->lock);
    Node* preds[MAX_LEVEL + 1];
    Node* curr = list->head;
    Node* victim = NULL;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* next = UNMARK_PTR(atomic_load(&curr->next[level]));
        while (next != list->tail && next->key < key) {
            curr = next;
            next = UNMARK_PTR(atomic_load(&curr->next[level]));
        }
        preds[level] = curr;
        if (level == 0) {
            if (next != list->tail && next->key == key) victim = next;
            else {
                omp_unset_lock(&list->lock);
                return false;
            }
        }
    }
    
    for (int i = 0; i <= victim->topLevel; i++) {
        Node* succ = UNMARK_PTR(atomic_load(&victim->next[i]));
        atomic_store(&preds[i]->next[i], succ);
    }
    
    atomic_fetch_sub(&list->size, 1);
    omp_unset_lock(&list->lock);
    omp_destroy_lock(&victim->lock);
    free(victim);
    return true;
}

bool skiplist_contains_coarse(SkipList* list, int key) {
    omp_set_lock(&list->lock);
    Node* curr = list->head;
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* next = UNMARK_PTR(atomic_load(&curr->next[level]));
        while (next != list->tail && next->key < key) {
            curr = next;
            next = UNMARK_PTR(atomic_load(&curr->next[level]));
        }
        if (level == 0) {
            bool found = (next != list->tail && next->key == key);
            omp_unset_lock(&list->lock);
            return found;
        }
    }
    omp_unset_lock(&list->lock);
    return false;
}

void skiplist_destroy_coarse(SkipList* list) {
    Node* curr = list->head;
    while (curr != NULL) {
        Node* next = UNMARK_PTR(atomic_load(&curr->next[0]));
        omp_destroy_lock(&curr->lock);
        free(curr);
        curr = next;
    }
    omp_destroy_lock(&list->lock);
    free(list);
}