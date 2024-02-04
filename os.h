void os_metrics_init(void);
U64 os_timer_freq(void);
U64 os_read_timer(void);
U64 os_file_size(char *filepath);
U64 os_process_page_fault_count(void);
U64 os_max_random_count(void);
bool os_random_bytes(void *dest, U64 dest_size);
