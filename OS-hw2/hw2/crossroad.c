#include "crossroad.h"
#include "hw2_output.h"
#include <pthread.h>
#include <stdlib.h>
// TODO: Define your global intersection state variables here
// e.g., dynamically allocated arrays for lane queues

typedef struct Node {
    int car_id;
    long arrival_time; 
    struct Node* next;
} Node;

typedef struct {
    Node* head;  // front of queue (dequeue here)
    Node* tail;  // back of queue (enqueue here)
} Queue;

void enqueue(Queue* currQueue, Node* node){
    if(currQueue->head==NULL){
        currQueue->head = node;
        currQueue->tail=node;
    }
    else{
        currQueue->tail->next = node;
        currQueue->tail = node;
    }
}

Node* dequeue(Queue *currQueue){
    Node * node;

    if (currQueue->head == NULL) {
        return NULL;  // empty queue
    }

    else if(currQueue->tail==currQueue->head){ //onevar
        node = currQueue->head;
        currQueue->head = NULL;
        currQueue->tail=NULL;
        return node;
    }
    else{
        node = currQueue->head;
        currQueue->head=currQueue->head->next;
        return node;
    }

}

Node* peek_front_of_queue(Queue *currQueue){
    return currQueue->head;
}
int is_queue_empty(Queue *currQueue){
    return (currQueue->head)==NULL;
}


pthread_mutex_t mtx;
pthread_cond_t someone_exited;
Queue lane_waiting_queues[2][15]; //we will add waiting cars in the queues //hor = 0
int driver_in_horizontal_direction; //to check before entering
int driver_in_vertical_direction;
int driver_count[2][15];
int last_arrival_time; 
int lane_numbers[2];
int priorities[2][15];

int is_front_of_the_lane_queue(int car_id, Direction dir, int lane){
    return peek_front_of_queue(&(lane_waiting_queues[dir][lane]))->car_id == car_id;
}

int is_there_car_in_the_crossing_road(int car_id, Direction dir, int lane){
    if(dir==DIR_HORIZONTAL){
        return driver_in_vertical_direction;
    }
    else{
        return driver_in_horizontal_direction;
    }
}
int is_there_car_in_same_lane(int car_id, Direction dir, int lane){
    return driver_count[dir][lane]; 
}
int is_there_higher_priority_waiting(int car_id, Direction dir, int lane){
    //check for higher priority waiting, or same priority ,earlier arrival time.
    int curr_prio=priorities[dir][lane];
    int arr_curr =peek_front_of_queue(&lane_waiting_queues[dir][lane])->arrival_time;
    Direction cross = (dir == DIR_HORIZONTAL) ? DIR_VERTICAL : DIR_HORIZONTAL;

    for (int i = 0; i < 15; i++) {
        if(!is_queue_empty(&lane_waiting_queues[cross][i])){
            if(priorities[cross][i]>curr_prio){
                return 1;
            }
            else if(priorities[cross][i]==curr_prio){
                int arr_1 = peek_front_of_queue(&lane_waiting_queues[cross][i])->arrival_time;
                if(arr_1 < arr_curr) return 1; 
            }
            else{continue;}
        }
    }

    return 0;

}

int can_enter(int car_id, Direction dir, int lane){

    if((!is_there_car_in_the_crossing_road(car_id,dir,lane))&&(!is_there_car_in_same_lane(car_id,dir,lane))&&is_front_of_the_lane_queue(car_id,dir,lane)&&(!is_there_higher_priority_waiting(car_id,dir,lane))){
        return 1;
    }
    return 0;
}

void initialize_crossroad(int h_lanes, int v_lanes, int* h_pri, int* v_pri) {

    // TODO: Dynamically allocate your state arrays based on the lane counts
    // TODO: Initialize your mutexes and condition variables
    pthread_mutex_init(&mtx, NULL);
    pthread_cond_init(&someone_exited, NULL);

    lane_numbers[0] = h_lanes;
    lane_numbers[1] = v_lanes;

    for (int i = 0; i < h_lanes; i++) priorities[0][i] = h_pri[i];
    for(int i = h_lanes; i < 15; i++) priorities[0][i] = -1;
    for (int i = 0; i < v_lanes; i++) priorities[1][i] = v_pri[i];
    for(int i = v_lanes; i < 15; i++) priorities[1][i] = -1;

    for (int d = 0; d < 2; d++) {
        for (int i = 0; i < 15; i++) {
            lane_waiting_queues[d][i].head = NULL; //0 for horizontal
            lane_waiting_queues[d][i].tail = NULL; //1 for vertical
        }
    }

    for (int d = 0; d < 2; d++) {
        for (int i = 0; i < 15; i++) {
            driver_count[d][i] = 0; 
        }
    }

    driver_in_horizontal_direction = 0;
    driver_in_vertical_direction = 0;
    last_arrival_time = 0;
}

void arrive_crossroad(int car_id, Direction dir, int lane) {
    // 1. Log arrival
    pthread_mutex_lock(&mtx);
    hw2_write_output(car_id, ET_ARRIVE, dir, lane);

    Node* new_node = malloc(sizeof(Node));
    new_node->car_id = car_id;
    new_node->arrival_time = last_arrival_time;
    new_node->next = NULL;

    last_arrival_time++;
    enqueue(&(lane_waiting_queues[dir][lane]),new_node);

    while (!can_enter(car_id, dir, lane)) {
        pthread_cond_wait(&someone_exited, &mtx);
    }

    Node* removed = dequeue(&lane_waiting_queues[dir][lane]);
    free(removed);
    driver_count[dir][lane]++;
    if(dir){driver_in_vertical_direction++;}
    else{driver_in_horizontal_direction++;}

    // 3. Log entry
    hw2_write_output(car_id, ET_ENTER, dir, lane);
    pthread_mutex_unlock(&mtx);
}

void exit_crossroad(int car_id, Direction dir, int lane) {

    pthread_mutex_lock(&mtx);
    
    driver_count[dir][lane]--;
    if(dir){driver_in_vertical_direction--;}
    else{driver_in_horizontal_direction--;}

    // 2. Log exit
    hw2_write_output(car_id, ET_EXIT, dir, lane);

    // 3. Signal waiting threads
    pthread_cond_broadcast(&someone_exited);
    pthread_mutex_unlock(&mtx);
}