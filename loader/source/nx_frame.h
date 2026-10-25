/* nx_frame.h -- раскладка кадра главного потока по фазам player loop (строки [frame]) */
#pragma once
#include <stdint.h>
#include <stddef.h>

void nx_frame_install(uintptr_t unity_base, size_t unity_size);
/* nx_hook.c: замер функций внутри FinishFrameRendering (строки [frame-r]) */
void nx_hook_install(uintptr_t unity_base, size_t unity_size);
void nx_hook_report(double frames);
void nx_hook_main_wait(uint64_t ticks);
/* вызовы из цикла кадра в main.c, только главный поток */
void nx_frame_loop_begin(void);
void nx_frame_render_begin(void);
void nx_frame_render_end(void);
uint64_t nx_frame_number(void);   /* номер кадра движка (растёт в nx_frame_render_end) */
double nx_frame_interval_ms(void); /* сглаженный интервал между показами кадров, мс */
/* счётчики с любых потоков */
void nx_frame_add_dequeue(uint64_t ticks);
void nx_frame_add_gc(uint64_t ticks);
/* ожидание главного потока (futex, сон): относится к текущей фазе кадра */
int nx_frame_on_main(void);
void nx_frame_main_wait(uint64_t ticks);
struct timespec;
int nx_frame_nanosleep(const struct timespec *req, struct timespec *rem);
int nx_frame_usleep(unsigned int us);
/* перед показом кадра (поток рендера): интервал показа по config.swap_interval */
void nx_frame_present(void *nwindow);
/* main.c: текущий targetFrameRate (лимит игры или fps_limit; 0 -- ещё не задан) */
int nx_fps_target(void);
