// Shared memory

// Limits
#define MAXKSMSZ    1048576 // max size of a shared memory segment in bytes
#define MINKSMSZ    4096    // min size of a shared memory segment in bytes

// Error codes
#define EINVAL    -1   // Invalid id asked to be attached to
#define ENOIDS    -2   // All id's used
#define ENOKEY    -3   // ID that does not have a corresponding key
#define ENOTDT    -4   // Tried to delete an id before detaching from it 
#define ENOTAT    -5   // Tried to detach an id before attaching to it
#define ENOTEXIST -6   // Tried to ksmget existing key with MUSTNOTEXISTS
#define EEXISTS   -7   // Tried to ksmget new key with MUSTEXIST

// Flags
#define KSM_MUSTNOTEXIST  0x01
#define KSM_MUSTEXIST     0x02

// Data structures
struct ksmglobalinfo_t {
  uint total_shrg_nr;   // number of attached shared memory segments
  uint total_shpg_nr;   // number of attached shared memory pages
};

struct ksminfo_t {
  uint ksmsz;                                 // shared memory segment size
  int cpid;                                   // PID of the creator
  int mpid;                                   // PID of the last modifier
  uint attached_nr;                           // number of attached virtual memory regions
  uint atime;                                 // last attach time
  uint dtime;                                 // last detach time
  struct ksmglobalinfo_t* ksm_global_info;    // address of global shared memory information structure
};
