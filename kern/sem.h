#ifndef JOS_INC_SEM_H
#define JOS_INC_SEM_H

struct sem {
    int val;
};

void
sem_post(struct sem *sem);
void
sem_wait(struct sem *sem);

#endif /* JOS_INC_SEM_H */
