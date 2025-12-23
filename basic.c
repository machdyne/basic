/*
 * Machdyne BASIC
 * Copyright (c) 2025 Lone Dynamics Corporation. All rights reserved.
 *
 * Refactored to support both command mode and INPUT statement mode
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#ifdef TARGET_LINUX
#include <ctype.h>
#else
int isalpha(int c);
int isdigit(int c);
int toupper(int c);
#endif

#define MAX_PROG 1024
#define MAX_LINE 64
#define NUM_VARS 26

enum {
    TOK_EOL = 0,
    TOK_LET,
    TOK_PRINT,
    TOK_GOTO,
    TOK_END,
    TOK_VAR,
    TOK_NUM,
    TOK_PLUS,
    TOK_MINUS,
    TOK_MUL,
    TOK_DIV,
    TOK_EQ,
    TOK_STR,
    TOK_IF,
    TOK_THEN,
    TOK_ELSE,
    TOK_LT,
    TOK_GT,
    TOK_LE,
    TOK_GE,
    TOK_NE,
    TOK_EQEQ,
    TOK_INPUT,
    TOK_PEEK,
    TOK_POKE,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_COMMA
};

static uint8_t program[MAX_PROG];
static uint16_t prog_len;
static int16_t vars[NUM_VARS];
static uint8_t memory[256];  /* Memory for PEEK/POKE */

/* Input routing state */
typedef enum {
    INPUT_MODE_COMMAND,           // Normal command interface
    INPUT_MODE_AWAITING_INPUT     // Program is waiting for INPUT statement
} input_mode_t;

static input_mode_t current_input_mode = INPUT_MODE_COMMAND;
static uint8_t *execution_pc = NULL;  // Saved program counter during INPUT

static int16_t expr(uint8_t **pc);
static void run_from(uint8_t *start_pc);

/* ================= INPUT ROUTING ================= */

// Main entry point from ls10.c - routes based on current mode
void basic_yield(uint8_t *line);

/* ================= TOKENIZER ================= */

static uint8_t *emit(uint8_t *p, uint8_t v) {
    *p++ = v;
    return p;
}

static uint8_t *emit_num(uint8_t *p, int16_t v) {
    *p++ = TOK_NUM;
    *p++ = v & 0xFF;
    *p++ = v >> 8;
    return p;
}

static uint8_t *emit_str(uint8_t *p, char *str, int len) {
    *p++ = TOK_STR;
    *p++ = len;
    memcpy(p, str, len);
    return p + len;
}

static int tokenize(char *src, uint8_t *out) {
    uint8_t *p = out;

    while (*src) {
        while (*src == ' ') src++;

        if (*src == '"') {
            src++;
            char *start = src;
            int len = 0;
            while (*src && *src != '"') {
                src++;
                len++;
            }
            if (*src == '"') src++;
            p = emit_str(p, start, len);
        }
        else if (isdigit(*src)) {
            int v = 0;
            while (isdigit(*src))
                v = v * 10 + (*src++ - '0');
            p = emit_num(p, v);
        }
        else if (isalpha(*src)) {
            if (!strncmp(src, "PRINT", 5)) {
                p = emit(p, TOK_PRINT);
                src += 5;
            } else if (!strncmp(src, "INPUT", 5)) {
                p = emit(p, TOK_INPUT);
                src += 5;
            } else if (!strncmp(src, "THEN", 4)) {
                p = emit(p, TOK_THEN);
                src += 4;
            } else if (!strncmp(src, "ELSE", 4)) {
                p = emit(p, TOK_ELSE);
                src += 4;
            } else if (!strncmp(src, "GOTO", 4)) {
                p = emit(p, TOK_GOTO);
                src += 4;
            } else if (!strncmp(src, "PEEK", 4)) {
                p = emit(p, TOK_PEEK);
                src += 4;
            } else if (!strncmp(src, "POKE", 4)) {
                p = emit(p, TOK_POKE);
                src += 4;
            } else if (!strncmp(src, "LET", 3)) {
                p = emit(p, TOK_LET);
                src += 3;
            } else if (!strncmp(src, "END", 3)) {
                p = emit(p, TOK_END);
                src += 3;
            } else if (!strncmp(src, "IF", 2)) {
                p = emit(p, TOK_IF);
                src += 2;
            } else {
                p = emit(p, TOK_VAR);
                p = emit(p, toupper(*src++) - 'A');
            }
        }
        else {
            if (*src == '<' && src[1] == '=') {
                p = emit(p, TOK_LE);
                src += 2;
            } else if (*src == '>' && src[1] == '=') {
                p = emit(p, TOK_GE);
                src += 2;
            } else if (*src == '<' && src[1] == '>') {
                p = emit(p, TOK_NE);
                src += 2;
            } else if (*src == '=' && src[1] == '=') {
                p = emit(p, TOK_EQEQ);
                src += 2;
            } else {
                switch (*src++) {
                    case '+': p = emit(p, TOK_PLUS); break;
                    case '-': p = emit(p, TOK_MINUS); break;
                    case '*': p = emit(p, TOK_MUL); break;
                    case '/': p = emit(p, TOK_DIV); break;
                    case '=': p = emit(p, TOK_EQ); break;
                    case '<': p = emit(p, TOK_LT); break;
                    case '>': p = emit(p, TOK_GT); break;
                    case '(': p = emit(p, TOK_LPAREN); break;
                    case ')': p = emit(p, TOK_RPAREN); break;
                    case ',': p = emit(p, TOK_COMMA); break;
                }
            }
        }
    }

    *p++ = TOK_EOL;
    return p - out;
}

/* ================= EXPRESSIONS ================= */

static int16_t factor(uint8_t **pc) {
    int16_t v = 0;

    if (**pc == TOK_NUM) {
        (*pc)++;
        v = (*pc)[0] | ((*pc)[1] << 8);
        *pc += 2;
    }
    else if (**pc == TOK_VAR) {
        (*pc)++;
        v = vars[*(*pc)++];
    }
    else if (**pc == TOK_STR) {
        (*pc)++;
        uint8_t len = *(*pc)++;
        *pc += len;
        v = 0;
    }
    else if (**pc == TOK_PEEK) {
        (*pc)++;
        if (**pc == TOK_LPAREN) (*pc)++;
        int16_t addr = expr(pc);
        if (**pc == TOK_RPAREN) (*pc)++;
        v = memory[addr & 0xFF];
    }
    else if (**pc == TOK_LPAREN) {
        (*pc)++;
        v = expr(pc);
        if (**pc == TOK_RPAREN) (*pc)++;
    }
    return v;
}

static int16_t term(uint8_t **pc) {
    int16_t v = factor(pc);
    while (**pc == TOK_MUL || **pc == TOK_DIV) {
        uint8_t op = *(*pc)++;
        int16_t rhs = factor(pc);
        if (op == TOK_MUL) v *= rhs;
        else if (rhs) v /= rhs;
    }
    return v;
}

static int16_t expr(uint8_t **pc) {
    int16_t v = term(pc);
    while (**pc == TOK_PLUS || **pc == TOK_MINUS) {
        uint8_t op = *(*pc)++;
        int16_t rhs = term(pc);
        if (op == TOK_PLUS) v += rhs;
        else v -= rhs;
    }
    return v;
}

static int condition(uint8_t **pc) {
    int16_t lhs = expr(pc);
    uint8_t op = *(*pc)++;
    int16_t rhs = expr(pc);
    
    switch (op) {
        case TOK_LT: return lhs < rhs;
        case TOK_GT: return lhs > rhs;
        case TOK_LE: return lhs <= rhs;
        case TOK_GE: return lhs >= rhs;
        case TOK_NE: return lhs != rhs;
        case TOK_EQEQ: return lhs == rhs;
        case TOK_EQ: return lhs == rhs;
    }
    return 0;
}

/* ================= EXECUTION ================= */

static uint8_t *find_line(uint16_t line) {
    uint8_t *p = program;
    while (p < program + prog_len) {
        uint16_t ln = p[0] | (p[1] << 8);
        if (ln == line) return p;
        p += 3 + p[2];
    }
    return NULL;
}

static void delete_line(uint16_t ln) {
    uint8_t *p = program;

    while (p < program + prog_len) {
        uint16_t cur = p[0] | (p[1] << 8);
        uint8_t len = p[2];
        uint16_t total = 3 + len;

        if (cur == ln) {
            memmove(p, p + total,
                    (program + prog_len) - (p + total));
            prog_len -= total;
            return;
        }
        p += total;
    }
}

static void insert_line(uint16_t ln, uint8_t *buf, int len) {
    uint8_t *p = program;
    
    // Find insertion point
    while (p < program + prog_len) {
        uint16_t cur = p[0] | (p[1] << 8);
        if (cur > ln) break;
        p += 3 + p[2];
    }
    
    // Make space
    if (p < program + prog_len) {
        memmove(p + 3 + len, p, (program + prog_len) - p);
    }
    
    // Insert new line
    *p++ = ln & 0xFF;
    *p++ = ln >> 8;
    *p++ = len;
    memcpy(p, buf, len);
    
    prog_len += 3 + len;
}

// Handler for INPUT statement response
static uint8_t current_input_var = 0;

static void handle_input_response(uint8_t *line) {
    // Parse the input value
    int val = atoi((char*)line);
    vars[current_input_var] = val;
    
    // Resume execution from where we left off
    if (execution_pc) {
        run_from(execution_pc);
        execution_pc = NULL;
    }
}

static void request_input(void) {
    current_input_mode = INPUT_MODE_AWAITING_INPUT;
    printf("? ");
#ifdef TARGET_LINUX
    fflush(stdout);
#endif
}

static void run_from(uint8_t *start_pc) {
    uint8_t *pc = start_pc;

    while (pc < program + prog_len) {
        uint8_t *ip = pc + 3;
        int should_advance = 1;  // Flag to control if we advance to next line

        switch (*ip++) {
            case TOK_LET: {
                if (*ip++ != TOK_VAR) break;
                uint8_t v = *ip++;
                ip++; /* = */
                vars[v] = expr(&ip);
                break;
            }
            
            case TOK_INPUT: {
                if (*ip == TOK_STR) {
                    ip++;
                    uint8_t len = *ip++;
                    printf("%.*s", len, (char*)ip);
                    ip += len;
                    if (*ip == TOK_COMMA) ip++;
                }
                if (*ip == TOK_VAR) {
                    ip++;
                    current_input_var = *ip++;
                    
                    // Save execution state and request input
                    execution_pc = pc + 3 + pc[2];  // Next line
                    request_input();
                    return;  // Exit and wait for input
                }
                break;
            }
            
            case TOK_POKE: {
                int16_t addr = expr(&ip);
                if (*ip == TOK_COMMA) ip++;
                int16_t val = expr(&ip);
                memory[addr & 0xFF] = val & 0xFF;
                break;
            }
            
            case TOK_PRINT:
                if (*ip == TOK_STR) {
                    ip++;
                    uint8_t len = *ip++;
                    printf("%.*s\r\n", len, (char*)ip);
                    ip += len;
                } else {
                    printf("%d\r\n", expr(&ip));
                }
                break;

            case TOK_IF: {
                int cond = condition(&ip);
                if (*ip == TOK_THEN) ip++;
                
                if (cond) {
                    // Execute the THEN part
                    uint8_t *else_pos = ip;
                    int depth = 0;
                    
                    // Find ELSE at same depth
                    while (*else_pos != TOK_EOL) {
                        if (*else_pos == TOK_IF) depth++;
                        else if (*else_pos == TOK_ELSE && depth == 0) break;
                        else if (*else_pos == TOK_NUM) else_pos += 2;
                        else if (*else_pos == TOK_STR) {
                            else_pos++;
                            else_pos += *else_pos + 1;
                            continue;
                        }
                        else if (*else_pos == TOK_VAR) else_pos++;
                        else_pos++;
                    }
                    
                    // Execute THEN clause
                    while (ip < else_pos && *ip != TOK_EOL) {
                        uint8_t tok = *ip++;
                        if (tok == TOK_PRINT) {
                            if (*ip == TOK_STR) {
                                ip++;
                                uint8_t len = *ip++;
                                printf("%.*s\r\n", len, (char*)ip);
                                ip += len;
                            } else {
                                printf("%d\r\n", expr(&ip));
                            }
                        } else if (tok == TOK_GOTO) {
                            uint8_t *new_pc = find_line(expr(&ip));
                            if (new_pc) {
                                pc = new_pc;
                                should_advance = 0;
                                break;
                            }
                        } else if (tok == TOK_LET) {
                            if (*ip++ == TOK_VAR) {
                                uint8_t v = *ip++;
                                ip++;
                                vars[v] = expr(&ip);
                            }
                        }
                    }
                } else {
                    // Skip to ELSE or EOL
                    int depth = 0;
                    while (*ip != TOK_EOL) {
                        if (*ip == TOK_IF) depth++;
                        else if (*ip == TOK_ELSE && depth == 0) {
                            ip++;
                            break;
                        }
                        if (*ip == TOK_NUM) ip += 2;
                        else if (*ip == TOK_STR) {
                            ip++;
                            ip += *ip + 1;
                            continue;
                        }
                        else if (*ip == TOK_VAR) ip++;
                        ip++;
                    }
                    
                    // Execute ELSE clause
                    while (*ip != TOK_EOL) {
                        uint8_t tok = *ip++;
                        if (tok == TOK_PRINT) {
                            if (*ip == TOK_STR) {
                                ip++;
                                uint8_t len = *ip++;
                                printf("%.*s\r\n", len, (char*)ip);
                                ip += len;
                            } else {
                                printf("%d\r\n", expr(&ip));
                            }
                        } else if (tok == TOK_GOTO) {
                            uint8_t *new_pc = find_line(expr(&ip));
                            if (new_pc) {
                                pc = new_pc;
                                should_advance = 0;
                                break;
                            }
                        } else if (tok == TOK_LET) {
                            if (*ip++ == TOK_VAR) {
                                uint8_t v = *ip++;
                                ip++;
                                vars[v] = expr(&ip);
                            }
                        }
                    }
                }
                break;
            }

            case TOK_GOTO: {
                uint8_t *new_pc = find_line(expr(&ip));
                if (new_pc) {
                    pc = new_pc;
                    should_advance = 0;
                }
                break;
            }

            case TOK_END:
                return;
        }
        
        if (should_advance) {
            pc += 3 + pc[2];
        }
    }
}

static void run(void) {
    run_from(program);
}

/* ================= LIST ================= */

static void print_token(uint8_t **ip) {
    switch (*(*ip)++) {
        case TOK_LET:   printf("LET "); break;
        case TOK_PRINT: printf("PRINT "); break;
        case TOK_INPUT: printf("INPUT "); break;
        case TOK_GOTO:  printf("GOTO "); break;
        case TOK_END:   printf("END"); break;
        case TOK_IF:    printf("IF "); break;
        case TOK_THEN:  printf("THEN "); break;
        case TOK_ELSE:  printf("ELSE "); break;
        case TOK_PEEK:  printf("PEEK"); break;
        case TOK_POKE:  printf("POKE "); break;

        case TOK_VAR:
            printf("%c", 'A' + *(*ip)++);
            break;

        case TOK_NUM: {
            int16_t v = (*ip)[0] | ((*ip)[1] << 8);
            *ip += 2;
            printf("%d", v);
            break;
        }

        case TOK_STR: {
            uint8_t len = *(*ip)++;
            printf("\"%.*s\"", len, (char*)*ip);
            *ip += len;
            break;
        }

        case TOK_PLUS:  printf(" + "); break;
        case TOK_MINUS: printf(" - "); break;
        case TOK_MUL:   printf(" * "); break;
        case TOK_DIV:   printf(" / "); break;
        case TOK_EQ:    printf(" = "); break;
        case TOK_LT:    printf(" < "); break;
        case TOK_GT:    printf(" > "); break;
        case TOK_LE:    printf(" <= "); break;
        case TOK_GE:    printf(" >= "); break;
        case TOK_NE:    printf(" <> "); break;
        case TOK_EQEQ:  printf(" == "); break;
        case TOK_LPAREN: printf("("); break;
        case TOK_RPAREN: printf(")"); break;
        case TOK_COMMA:  printf(", "); break;

        case TOK_EOL:
            break;
    }
}

static void list_program(void) {
    uint8_t *p = program;

    while (p < program + prog_len) {
        uint16_t ln = p[0] | (p[1] << 8);
        uint8_t len = p[2];
        uint8_t *ip = p + 3;

        printf("%u ", ln);
        while (*ip != TOK_EOL)
            print_token(&ip);
        printf("\r\n");

        p += 3 + len;
    }
}

/* ================= COMMAND PROCESSING ================= */

static void process_command(uint8_t *line) {
    if (!strncmp((char*)line, "RUN", 3)) {
        run();
        return;
    }
    if (!strncmp((char*)line, "LIST", 4)) {
        list_program();
        return;
    }

    uint16_t ln = atoi((char*)line);
    if (find_line(ln) != NULL) delete_line(ln);
    char *src = strchr((char*)line, ' ');
    if (!src) return;

    uint8_t buf[64];
    int len = tokenize(src + 1, buf);

    insert_line(ln, buf, len);
}

/* ================= INPUT ROUTING ================= */

void basic_yield(uint8_t *line) {
    if (current_input_mode == INPUT_MODE_AWAITING_INPUT) {
        // Deliver line to INPUT statement handler directly
        handle_input_response(line);
        
        // Reset to command mode
        current_input_mode = INPUT_MODE_COMMAND;
    } else {
        // Normal command processing
        process_command(line);
    }
}

/* ================= MAIN (Linux only) ================= */

#ifdef TARGET_LINUX

int main(void) {
    char line[MAX_LINE];

    puts("Machdyne BASIC");

    while (1) {
        if (current_input_mode == INPUT_MODE_COMMAND) {
            printf("> ");
        }
        fflush(stdout);
        
        if (!fgets(line, sizeof(line), stdin))
            break;

        basic_yield((uint8_t*)line);
    }
    
    return 0;
}

#endif
