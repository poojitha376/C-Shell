#ifndef JOBS_H
#define JOBS_H



void add_job(pid_t pid, char *command);
void remove_job(pid_t pid);
void print_jobs();
void update_job_status();
Job *find_job_by_id(int job_id);
Job *find_job_by_pid(pid_t pid);

#endif