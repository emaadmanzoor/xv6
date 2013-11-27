#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]) {

 int semFull;
 int semEmpty;
 int semMutex;
 int i, j;

 int id = ksmget(1, 10*sizeof(char), 0);
 char* buffer = ksmattach(id, 0);

 for(i = 0; i < 10; i++) {
   buffer[i] = 'E';
 }

 int pid;
 semEmpty = sem_get(100, 10);
 semFull = sem_get(200, 0);
 semMutex = sem_get(300, 1);
 
 printf(1,"semEmpty %d\n", semEmpty);
 printf(1,"semFull %d\n", semFull);
 printf(1,"semMutex %d\n", semMutex);

 printf(1, "producer PID = %d\n", getpid());
 pid = fork();

 // Producer
 if(pid != 0) {
   //while(1) {
   for(j = 0; j < 100; j++) {
     printf(1, "\nProducer\n", pid);
     sem_wait(semEmpty);
     sem_wait(semMutex);
     for(i = 0; i < 10; i++) {
       if (buffer[i] == 'E') {
         buffer[i] = 'F';
         break;
       }
     }
     for(i = 0; i < 10; i++) {
       printf(1, "prod:%c ", buffer[i]);
     }
     printf(1, "\n");
     sem_signal(semMutex);
     sem_signal(semFull);
   }
   wait();
   ksmdelete(id);
 // Consumer
 } else {
   //while(1) {
   int id = ksmget(1, 10*sizeof(char), 0);
   char* buffer = ksmattach(id, 0);
   for(j = 0; j < 100; j++) {
     printf(1, "\n Consumer\n");
     sem_wait(semFull);
     sem_wait(semMutex);

     for(i = 0; i < 10; i++) {
       if (buffer[i] == 'F') {
         buffer[i] = 'E';
         break;
       }
     }
    
     for(i = 0; i < 10; i++) {
       printf(1, "cons:%c ", buffer[i]);
     }

     printf(1, "\n");
     sem_signal(semMutex);
     sem_signal(semEmpty);
   }
 }
 exit();
}
