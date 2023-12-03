/*
�ù��ߵ�������򣬰���ͻ�����ӵĶ����ɸѡ�Լ����������Ĳ�����ɸѡ����ģ�飨ע��ͻ�����ӵĸ��ʼ���Ҳ�ڱ����֣����÷��������滻ģ������ԭ��������randum-fuzz����
*/

#define AFL_MAIN
#define MESSAGES_TO_STDOUT

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#ifdef __cplusplus
extern "C" {
#endif
#include "config.h"
#include "types.h"
#include "debug.h"
#include "alloc-inl.h"
#include "hash.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <ctype.h>
#include <fcntl.h>

#include <string.h>

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#ifdef __cplusplus
}
#endif

#include <vector>
#include <string>
#include <iostream>
#include <fstream> 

static char in_dir[256];
static char out_dir[256];
static int max_trials;
static long long start_time;
static long long end_time;

struct queue_entry {
  char fname[256];                      /* �����������ļ���      */
  u32 len;                            /* �������볤��                     */

  u8  cal_failed,                     /* ���򹹽��Ƿ�ɹ�              */
      trim_done,                      /* �Ƿ���ɼ�֦                         */
      was_fuzzed,                     /* �Ƿ��Ѿ������ģ������        */
      passed_det,                     /* �Ƿ��Ѿ������ͻ�����ӵ�ѡ��     */
      has_new_cov,                    /* �Ƿ�������µĸ���         */
      var_behavior,                   /* ��������Ϊ               */
      favored,                        /* ��ͻ�������Ƿ��Ϊ��Ч         */
      fs_redundant;                   /* ���������Ƿ�����  */

  u32 bitmap_size,                    /* ���ڼ�¼ִ��·���ı��ر�     */
      exec_cksum;                     /* ��ִ��·������������м����  */

  u64 exec_us,                        /* ִ��ʱ��             */
      handicap,                       /* ѭ��������   */
      depth;                          /* ·�����                     */

  u8* trace_mini;                     /* ����·���ļ�¼             */
  u32 tc_ref;                         /* ��·�����õļ�¼           */

  struct queue_entry *next,           /* ��һ��Ԫ��             */
                     *next_100;       /* ��100��Ԫ��               */

};

struct characteristic{
  char fname[256];
  u32 value;
};

static std::vector<queue_entry*> input_queue; /*��ʼִ��ģ�����Է��� */
std::vector<double> prob = {0.167, 0.167, 0.167, 0.167, 0.167, 0.167};  /*�����ʼÿ��ͻ�����ӱ�ѡ��ĸ���*/

static u8* trace_bits;                /* ·����ϣֵ�ı��ر� */
static u8  total_bits[MAP_SIZE];     /* ��¼Ŀǰ����󸲸�״�� */
static std::vector<characteristic*> divergence;  /* ��󸲸�����Ĺ�ϣ��ʾ*/
static std::vector<int> mut; 

static int child_pid = -1;            /* ģ�����Գ����PID     */
static int shm_id;                    /* SHM ID */
static u16 count_class_lookup16[65536];
static FILE* plot_file; 
static bool hardware_enabled = 0;     /*�򻯰汾����˲�ʹ��Ӳ��ģ����*/
static int current_max = 0;
static int input_max = 0;
static int input_min = 0;

static s32 out_fd,
           dev_urandom_fd = -1,
           out_dir_fd = -1,
           dev_null_fd = -1;

static u8 *out_file;

enum {
  /*00*/ NOT_INTEREST,
  /*01*/ NEW_COVERAGE,
  /*02*/ NEW_HARDWARE,
  /*03*/ NEW_BOTH
};

/* ͨ��״̬���������жϳ�������״̬ */
enum {
  /* 00 */ FAULT_NONE,
  /* 01 */ FAULT_CRASH,
  /* 02 */ FAULT_ERROR
};

/* ��ó�������ʱ�䣬��������������ʾ */

static u64 get_cur_time(void) {

  struct timeval tv;
  struct timezone tz;

  gettimeofday(&tv, &tz);

  return (tv.tv_sec * 1000ULL) + (tv.tv_usec / 1000);

}

/* �����Ӧ����ʾ��Ϣ */

static void usage(char* argv0) {

  SAYF("Usage: \n%s input_dir output_dir max_trials /path/to/fuzzed_app \n\n", argv0);

  exit(1);

}

/* Ϊ�˲��۸ĳ�ʼָ�����һ��ָ��� */

static void save_cmdline(u32 argc, char** argv) {

  u32 len = 1, i;
  char* buf;

  for (i = 0; i < argc; i++)
    len += strlen(argv[i]) + 1;
  
  buf = ck_alloc(len);

  for (i = 0; i < argc; i++) {

    u32 l = strlen(argv[i]);
//    printf("%s\n", argv[i]);
    memcpy(buf, argv[i], l);
    buf += l;

    if (i != argc - 1) *(buf++) = ' ';

  }

  *buf = 0;

}

std::string random_replace(const std::string &str) {
  srand(time(NULL));
  int n = str.size();
  int pos = rand() % n;
  char c = str[pos];
  c ^= (1 << rand() % 7);
  std::string ret(str);
  ret[pos] = c;
  return ret;
}

std::string random_append_int(const std::string &str) {
  srand(time(NULL));
  std::string ret(str);
  int num = rand();
  ret = ret + "\n" + std::to_string(num);
  return ret;
}

/*����ÿ��ͻ�����ӱ�ѡ�񵽵ĸ��ʽ��б���ͻ�����ӵ�ѡ��*/

int selection(){
    std::vector<int> prob_int;
    int sum = 0;
    for(int i=0;i<prob.size();i++) {
        sum += prob[i]*100;
        prob_int.push_back(sum);
    }
    int idx = rand()%100;
    int ret = 0;
    for (int i=0;i<prob_int.size();i++) {
        if(prob_int[i]>=idx) {
            ret = i;
            break;
        }
    }
    return ret;
}

std::string mutate(int fuzzing_iteration, std::string current_input){

  //printf("current input: %s\n", current_input.c_str());

  std::ifstream ifs(current_input);
  std::string content( (std::istreambuf_iterator<char>(ifs) ),
                       (std::istreambuf_iterator<char>()    ) );
  //std::cout << content;
  //printf("length %d\n", content.length());

  srand(time(0) + rand());
  //int knob = selection();
  //std::cout << s << std::endl;
  int knob = rand()%2+1;
  std::cout << knob << std::endl;
  if(knob == 1){
    srand(time(0) + rand());
    int pos = rand()%(content.length()-1);
  //  printf("selected pos: %d\n", pos);
    u8 new_value = rand()%256;
    content[pos] = new_value;
  }
  else if(knob == 2){
    content = random_append_int(content);
  }
  else if(knob == 3){
    content = random_replace(content);
  }
  else if(knob == 4){
    int pos = rand()%(content.length()-1);
    u8 new_value = '/n';
    content[pos] = new_value;
  }else{
    int pos = rand()%(content.length()-1);
    u8 new_value = rand()%256;
    content[pos] = new_value;
  }

  std::string mutated_input = std::string(out_dir) + std::to_string(fuzzing_iteration);
  //printf("%s\n", mutated_input.c_str());
  std::ofstream out(mutated_input);
  out << content;
  out.close();
  return mutated_input;
}

int run_target(char* app, char mutated_input[]){
  int status = 0;
  memset(trace_bits, 0, MAP_SIZE);

  // u32 ck2 = hash32(trace_bits, MAP_SIZE, HASH_CONST);
  // SAYF("check sum of 0 bitmap %u\n", ck2);

  char* argv[] = {app, mutated_input, NULL};

  child_pid = fork();
  if(child_pid < 0){
    perror("fork error.");
    exit(EXIT_FAILURE);
  }
  
  if(!child_pid){ 
    execv(app, argv);
    *(u32*)trace_bits = EXEC_FAIL_SIG;
    exit(0);
  }

  pid_t ret;
  ret = waitpid(child_pid, &status, 0);
  if(ret < 0){
    perror("wait error");
    exit(EXIT_FAILURE);
  }
  
  int tb4 = *(u32*)trace_bits;

  u32 ck1 = hash32(trace_bits, MAP_SIZE, HASH_CONST);
  SAYF("check sum of changed bitmap %u\n", ck1);

//  q->exec_cksum = ck1;

  if (!WIFSTOPPED(status)) child_pid = 0;

  if (WIFEXITED(status))
  {
    printf("child exited normal exit status= %d\n", WEXITSTATUS(status));
    return FAULT_NONE;
  }
  else if (WIFSIGNALED(status)){
    printf("child exited abnormal signal number= %d \n", WTERMSIG(status));
    return FAULT_CRASH;
  }
  //else if (WIFSTOPPED(status)){
  //   printf("child stoped signal number=%d\n", WSTOPSIG(status));
  //   return FAULT_ERROR;
  //}
  else if (tb4 == EXEC_FAIL_SIG){
    return FAULT_ERROR;
  }
       
}

bool larger(std::string current, int max){
  if(max > atoi(current.c_str())){
    return false;
  }
  else{
    max = atoi(current.c_str());
    return true;
  }
}

int check_new_hardware(){
  int ret_val;
  if(hardware_enabled){
    std::ifstream ifs("hls_report/solution1/*.rpt");
    std::string content( (std::istreambuf_iterator<char>(ifs) ),
                       (std::istreambuf_iterator<char>()    ) );
    if(larger(content, current_max)){
      ret_val = 1;
    }else ret_val = 0;
  }else{
    ret_val = 0;
  }
  return ret_val;
}

// ���ݳ���ִ���������ͻ�����ӱ�ѡ�񵽵ĸ��ʣ����㷽����ppt
void update_probability(){
      std::vector<int> prob_int;
    int sum = 0;
    for(int i=0;i<prob.size();i++) {
        if(mut[i]){
          prob[i] = prob[i] + 0.05;
        }
        else
        {
          prob[i] = prob[i] - 0.05/(mut.size()-1);
        }
        
        prob_int.push_back(sum);
    }
}

/*save the input if a new edge is covered or maximize a hardware divergence character
return 0 if not interested, return 1 if new coverage, return 2 if new hardware character,
return 3 if both;
����ò���������Ӳ��������ػ���������ģ�����Եĸ����ʣ��ͱ���ò���������
����ĺ�������0����ò���������Ч������1����������ģ�����Եĸ����ʣ�����2������Ӳ��������ء�����3�������߼�ߡ�

*/

int save_if_interest(){
  int ret_val = 0;
  int new_coverage = 0;
  int new_hardware = 0;

  for(int i = 0; i < (1<<16); ++i) {
    if(trace_bits[i] && !total_bits[i]) {
      total_bits[i] = 1;
      new_coverage = 1;
    }
  }

  new_hardware = check_new_hardware();
  
  if(new_coverage && new_hardware){
    update_probability();
    return NEW_BOTH;
  }else if(new_coverage && !new_hardware){
    return NEW_COVERAGE;
  }else if(!new_coverage && new_hardware){
    update_probability();
    return NEW_HARDWARE;
  }

  return NOT_INTEREST;
}

void write_to_test(std::string current_input, int interest){
  
  if(!interest) remove(current_input.c_str());
  else{
    struct queue_entry *q = (struct queue_entry *)malloc(sizeof(queue_entry));
    q->exec_cksum = hash32(trace_bits, MAP_SIZE, HASH_CONST);

    std::string new_name; 
    if(interest == NEW_COVERAGE){
      q->has_new_cov = 1;
      new_name = std::string(current_input) + "_cov";
    }
    else if(interest == NEW_HARDWARE) new_name = std::string(current_input) + "_hd";
    else if(interest == NEW_BOTH){
      q->has_new_cov = 1;
      new_name = std::string(current_input) + "_both";
    }
    rename(current_input.c_str(), new_name.c_str());
    memcpy(q->fname, new_name.c_str(), 256);
    input_queue.push_back(q);
  }
}

void write_to_test(std::string current_input){
  std::string new_name = std::string(current_input) + "_crash";
  rename(current_input.c_str(), new_name.c_str());
}

std::vector<std::string> SplitString(std::string &s, const std::string &delimiter) {
	size_t pos = 0;
	std::string token;
	std::vector<std::string> ret;
	while ((pos = s.find(delimiter)) != std::string::npos) {
		token = s.substr(0, pos);
		//std::cout << token << std::endl;
		ret.push_back(token);
		s.erase(0, pos + delimiter.length());
	}
	ret.push_back(s);
	return ret;
}

/*������������ı߽�Բ�����������ɸѡ���ò����õ�filter��������ͬ���в���������ɸѡ*/

bool worthy_simulation(std::string input){
  if(hardware_enabled){
    	std::vector<std::string> input_list = SplitString(input,"/n");
			for(auto arg:input_list) {
				if(atoi(arg.c_str()) > input_max) return true;
        else if(atoi(arg.c_str()) < input_min) return true;
        else return false;
			}
  }else{
    return true;
  }
}

/*����ģ�����ԣ����ѡ��һ�����룬ʹ��ѡ����ͻ�����ͻ����������������鸲���ʣ��������룬�ظ��������� 
 */

void fuzzing(char* app, int iteration){

  for(int i = 1; i < iteration; i ++){
    printf("\n**********%d**********\n", i);
    //printf("input queue length: %d\n", input_queue.size());
    // for(int i = 0; i < input_queue.size(); i++){
    //   printf("%s ", input_queue[i]->fname);
    // }
    srand(time(0) + rand());
    int index = rand()%input_queue.size();
  
    struct queue_entry* q = input_queue[index];
  //  static u8 first_trace[MAP_SIZE];

    std::string current_input = std::string(q->fname);
  //  std::cout << "selected input: " << current_input << std::endl;

  //  if (q->exec_cksum) memcpy(first_trace, trace_bits, MAP_SIZE);

    //u32 ck1 = hash32(trace_bits, MAP_SIZE, HASH_CONST);
    //SAYF("\nFirst trace: %u\n", ck1);

    std::string mutated_input = mutate(i, current_input);
    std::cout << "running with mutated input: " << mutated_input << std::endl ;
    char mutated[256] = "0";
    //strncpy(mutated, mutated_input.c_str(), mutated_input.length() + 1);
    
    if(worthy_simulation(mutated_input)){
      int crash = run_target(app, mutated_input.c_str());
    
      if(crash){ //if found crash
        write_to_test(mutated_input);
      }else{  // else check the guidance
        int interest = save_if_interest();
        printf("the current input is interest: %d\n", interest);
        write_to_test(mutated_input, interest);
        }
      }
  }
}


/* ��������������Լ�����������Ƿ���ȷ */

void perform_dry_run(char* app){
  ACTF("Attempting dry run with '%s'...", app);

  int status = 0;
  memset(trace_bits, 0, MAP_SIZE);

  char* argv[] = {app, "/home/qzhang/Downloads/afl-2.52b/good-seeds/anyseed", NULL};

  child_pid = fork();
  if(child_pid < 0){
    perror("fork error.");
    exit(EXIT_FAILURE);
  }
  
  if(!child_pid){ // This is child process
    printf("This is the child process");
    execv(app, argv);
    *(u32*)trace_bits = EXEC_FAIL_SIG;
    exit(0);
  }

  pid_t ret;
  ret = waitpid(child_pid, &status, 0);
  if(ret < 0){
    perror("wait error");
    exit(EXIT_FAILURE);
  }
  

//  int tb4 = *(u32*)trace_bits;

  u32 ck1 = hash32(trace_bits, MAP_SIZE, HASH_CONST);
  SAYF("check sum of changed bitmap %u\n", ck1);

  // FILE* f = fopen("1.dat", "w");
  // for(int i = 0; i < MAP_SIZE; i++)
  // {
  //   fprintf(f, "%d ", trace_bits[i]);
  // }
  // fclose(f);
  
  //�����������ӵĺ�
  input_queue[0]->exec_cksum = ck1;
  input_queue[0]->has_new_cov = 1;

  // �����ܸ�����
  memcpy(total_bits, trace_bits, sizeof(u8)*(1<<16));

  if (!WIFSTOPPED(status)) child_pid = 0;

  if (WIFEXITED(status))
        printf("child exited normal exit status= %d\n", WEXITSTATUS(status));

    else if (WIFSIGNALED(status))
        printf("child exited abnormal signal number= %d \n", WTERMSIG(status));
    else if (WIFSTOPPED(status))
        printf("child stoped signal number=%d\n", WSTOPSIG(status));

}

static void list_dir(const char *path)
{
    struct dirent *entry;

    DIR *dir = opendir(path);
    if (dir == NULL) {
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if(entry->d_name[0]=='.')
            continue;
        struct queue_entry *q = (struct queue_entry *)malloc(sizeof(queue_entry));
        //q->fname = entry->d_name;
        std::string file_name = std::string(path) + std::string(entry->d_name);
        memcpy(q->fname, file_name.c_str(), strlen(file_name.c_str()));
        input_queue.push_back(q);
    }

    closedir(dir);
}

static void setup_shm(){

  ACTF("Setting up the shared memory for code coverage...");
  u8* shm_str;

  memset(total_bits, 0, MAP_SIZE);

  shm_id = shmget(IPC_PRIVATE, MAP_SIZE, IPC_CREAT | IPC_EXCL | 0600);
  
  if (shm_id < 0) PFATAL("Fialed to creat a shared memory");
  
  shm_str = alloc_printf("%d", shm_id);
  
  setenv(SHM_ENV_VAR, shm_str, 1);

  ck_free(shm_str);


  trace_bits = shmat(shm_id, NULL, 0);
  if (!trace_bits) PFATAL("shmat() failed");
}


/* main���� */

int main(int argc, char** argv) {

  SAYF(cCYA "random-fuzz " cBRI VERSION cRST " by <zhangqian@cs.ucla.edu>\n");

  memset(in_dir, 0, 256);
  memset(out_dir, 0, 256);
  
  if(argc < 5)  usage(argv[0]);

  memcpy(in_dir, argv[1], strlen(argv[1]));
  memcpy(out_dir, argv[2], strlen(argv[2]));
  max_trials = atoi(argv[3]);


  if (!strcmp(in_dir, out_dir))
    FATAL("Input and output directories can't be the same");


  setup_shm();
  OKF("Shared memory is ready.");
  u32 ck1 = hash32(trace_bits, MAP_SIZE, HASH_CONST);
  SAYF("main cksum %d\n", ck1);
  
  list_dir(in_dir);
  OKF("Input queue initialized with %d seeds.", input_queue.size());
  // for(int i = 0; i < input_queue.size(); i++){
  //   printf("%s\n", input_queue[i]->fname);
  // }


  save_cmdline(argc, argv);
  OKF("Command line saved.");

  
  start_time = get_cur_time();
  OKF("The start time is: %lld", start_time);

 
  OKF("Perform dry run!");
  perform_dry_run(argv[4]);
  

  printf("Fuzzing execution time: %lld\n", end_time-start_time);

  OKF("The binary works well with the seed input.");
  

  OKF("Start fuzzing!");
  fuzzing(argv[4], max_trials);

  end_time = get_cur_time();
  OKF("The end time is: %lld\n", end_time);

  OKF("We're done here. Have a nice day!\n");

  exit(0);

}