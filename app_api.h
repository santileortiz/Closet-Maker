/*
 * Copyright (C) 2017 Santiago León O. <santileortiz@gmail.com>
 */

#if !defined(APP_API_H)

struct app_state_t {
    bool is_initialized;

    bool end_execution;
    struct gui_state_t gui_st;

    mem_pool_t memory;
};

#define APP_API_H
#endif
