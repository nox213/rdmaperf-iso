#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/mman.h>

#include "misc.h"
#include "resource_table.h"

#define TABLE_SIZE 100

struct resource *r_table;
struct task_info t_info;

int main(int argc, char *argv[])
{
	int i;
	char str[200];
	char *tok;
	size_t len;
	FILE *config_fp = NULL;
	int shm_fd;

	config_fp = fopen("config.txt", "r");
	if (!config_fp)
		err_sys("fail to open config file");

	shm_fd = shm_open("resource_table", O_RDWR | O_CREAT, 0644);
	ftruncate(shm_fd, TABLE_SIZE * sizeof(struct resource));
	r_table = mmap(NULL, TABLE_SIZE * sizeof(struct resource), PROT_READ | PROT_WRITE, 
			MAP_SHARED, shm_fd, 0);
	if (!r_table)
		goto free;

	init_task_info(&t_info);
	while (fgets(str, sizeof(str), config_fp)) {
		len = strlen(str);
		str[len-1] = '\0';

		tok = strtok(str, " ");
		i = atoi(tok);

		tok = strtok(NULL, " ");
		strncpy(r_table[i].task_name, tok, sizeof(r_table[i].task_name));
		r_table[i].task_name[sizeof(r_table[i].task_name)-1] = '\0';

		tok = strtok(NULL, " ");
		if (strcmp(tok, "latency") == 0)
			r_table[i].type = LATENCY;
		else
			r_table[i].type = SECONDARY;

		printf("id: %d, name: %s, type: %d\n", i, r_table[i].task_name, r_table[i].type);
		t_info->nr_task++;
	}

	fclose(config_fp);
	daemonize(argv[0]);
	monitor();

free:
	shm_unlink("resource_table");

	return 0;
}

int init_task_info(struct task_info *t_info, uint64_t total_bandwidth, uint64_t total_qps)
{
	t_info->nr_task = 0;
	t_info->total_bandwidth = total_bandwidth;
	t_info->total_qps = total_qps;
}

int monitor(void)
{
	init_resource();

	/* monitoring */
	return 0;
}

void init_resource(void)
{
}
