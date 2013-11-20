#include "types.h"
#include "user.h"
#include "param.h"
#include "ksm.h"

void printksm(int);

void printksm(int id) {
  return;
  struct ksmglobalinfo_t ksmglobalinfo;
  struct ksminfo_t ksminf = {.ksm_global_info = &ksmglobalinfo};
  ksminfo(id, &ksminf);
  printf(1, "\n");
  printf(1, "ksminfo(%d):\n", id);
  printf(1, "\tcpid\t\t\t%d\n", ksminf.cpid);
  printf(1, "\tmpid\t\t\t%d\n", ksminf.mpid);
  printf(1, "\tksmsz\t\t\t%d\n", ksminf.ksmsz);
  printf(1, "\tattached_nr\t\t%d\n", ksminf.attached_nr);
  printf(1, "\tatime\t\t\t%d\n", ksminf.atime);
  printf(1, "\tdtime\t\t\t%d\n", ksminf.dtime);
  printf(1, "\ttotal_shrg_nr\t\t%d\n", ksminf.ksm_global_info->total_shrg_nr);
  printf(1, "\ttotal_shpg_nr\t\t%d\n", ksminf.ksm_global_info->total_shpg_nr);
  printf(1, "\n");
}

void
ksmtest(void)
{
  int key, errcode, id, expectedId, large, onepage, pid1, pid2;

  // Initial id reservation
  for (key = 10; key < 10 + MAXKSMIDS; key++) {
    id = ksmget(key, 8192, 0);
    expectedId = key - 10 + 1;
    if (id != expectedId) {
      printf(1, "Failed ksmtest 1: got id %d for key %d, expected %d\n", id, key, expectedId);
      exit();
    }
  }

  void* ksm1 = ksmattach(10, 0); // attach 2 pages
  
  // Re-attachment should return the same address
  void* ksm2 = ksmattach(10, 0);
  if (ksm2 != ksm1) {
    printf(1, "Failed ksmtest 2: expected %x got %x\n", ksm1, ksm2);
    exit();
  }

  // Another attachment, 2 pages
  ksmattach(11, 0);

  // Detach the first 2 pages
  ksmdetach(10);

  // Attach 2 pages, should take the same va's as the first 2 pages
  void* ksm4 = ksmattach(12, 0);
  if (ksm4 != ksm1) {
    printf(1, "Failed ksmtest 3: expected %x got %x\n", ksm1, ksm4);
    exit();
  }

  // Reserve an id after all id's have been taken
  errcode = ksmget(key, 4096, 0);
  if (errcode > 0) {
    printf(1, "Failed ksmtest 4: expected ENOIDS, got %d\n", errcode);
  } 

  // Get the id for an existing key
  for (key = 10 + MAXKSMIDS - 1; key >= 10; key--) {
    id = ksmget(key, 4096, 0);
    expectedId = key - 10 + 1;
    if (id != expectedId) {
      printf(1, "Failed ksmtest 5: got id %d for key %d, expected %d\n", id, key, expectedId);
      exit();
    }
  }

  // Delete an invalid ID < 0
  errcode = ksmdelete(-5);
  if (errcode > 0) {
    printf(1, "Failed ksmtest 6, expected EINVAL, got %d\n", errcode);
    exit();
  }

  // Delete an invalid ID > MAXKSMIDS
  errcode = ksmdelete(MAXKSMIDS + 1);
  if (errcode > 0) {
    printf(1, "Failed ksmtest 7, expected EINVAL, got %d\n", errcode);
    exit();
  }

  // Delete id's 1-6, that haven't been attached
  for (id = 1; id <= 6; id++) {
    if (ksmdelete(id) < 0) {
      printf(1, "Failed ksmtest 8, deleting valid id %d", id);
      exit();
    }
  }

  for (key = 1; key <= 6; key++) {
    id = ksmget(key, 4096, 0);
    expectedId = key;
    if (id != expectedId) {
      printf(1, "Failed ksmtest 9: got id %d for key %d, expected %d\n", id, key, expectedId);
      exit();
    }
  }

  // Large KSM segment
  large = ksmget(1, 1000000, 0);
  if(id <= 0) {
    printf(1, "Failed ksmtest 10 large: got %d for key %d, expected > 0\n", large, 1);
  } 
  printksm(large);

  // One page KSM segment
  onepage = ksmget(2, 500, 0);
  if(onepage == large) {
    printf(1, "Failed ksmtest 11 onepage: got %d for key %d, expected != large\n", onepage, 2);
  }
  printksm(onepage);

  printf(1, "Attach, detach and delete tests\n");
  printf(1, "Pages used = %d\n", pgused());

  void* r = ksmattach(large, 0);
  if(r <= 0) {
    printf(1, "Failed ksmtest 12 attach large: got %x for id %d, expected > 0\n", r, large);
  }
  printksm(large);

  void* p = ksmattach(onepage, 0);
  if (p <= 0) {
    printf(1, "Failed ksmtest 13 attach onepage: got %x for id %d, expected > 0\n", p, onepage);
  }
  printksm(onepage);

  errcode = ksmdelete(large);
  if(errcode < 0) {
    printf(1, "Failed ksmtest 14 attach onepage: got %x for id %d, expected > 0\n", p, onepage);
  }
  printksm(large);

  printf(1, "Pages used = %d\n", pgused());

  errcode = ksmdetach(large);
  if(errcode < 0) {
    printf(1, "Failed ksmtest 15 attach large: got %x for id %d, expected > 0\n", r, large);
  }

  printf(1, "Pages used = %d\n", pgused());
  printf(1, "Fork tests\n");

  //printf(1, "parent (pid = %d) will now fork\n", getpid());
  pid1 = fork();
  if(pid1 != 0) {
    printksm(onepage);
    //printf(1, "parent (pid = %d) will now fork again\n", getpid());
    pid2 = fork();
    if(pid2 != 0) {
      printksm(onepage);
      //printf(1, "parent (pid = %d) will now wait\n", getpid());
      wait();
      printksm(onepage);
      //printf(1, "parent (pid = %d) will now wait again\n", getpid());
      wait();
    } else {
      exit();
    }
  } else {
    exit();
  }

  // Delete all attachments
  for (id = 1; id < MAXKSMIDS + 1; id++) {
    ksmdelete(id);
  }
  printf(1, "Passed ksmtest\n");
}

int
main(void)
{
  printf(1, "Pages used = %d\n", pgused());
  ksmtest();
  printf(1, "Pages used = %d\n", pgused());
  exit();
}
