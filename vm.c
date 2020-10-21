#include <stdint.h>
#include <stdio.h>
#include <signal.h>
/* windows only headers */
#include <windows.h>
#include <conio.h>

HANDLE hStdin = INVALID_HANDLE_VALUE;

/*registers 
8 general purpose regs [R_R0 - R_R7]
3 special purpose registers [R_PC, R_COND, R_COUNT])*/

enum{
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC, /* Program Counter */
    R_COND, /* Condition flag */
    R_COUNT /* Iteration code */
};

/* Conditional flags*/

enum{
    FL_POS = 1 << 0,
    FL_ZRO = 1 << 1,
    FL_NEG = 1 << 2
};

/*opcodes*/

enum
{
    OP_BR = 0, /* branch */
    OP_ADD,    /* add  */
    OP_LD,     /* load */
    OP_ST,     /* store */
    OP_JSR,    /* jump register */
    OP_AND,    /* bitwise and */
    OP_LDR,    /* load register */
    OP_STR,    /* store register */
    OP_RTI,    /* unused */
    OP_NOT,    /* bitwise not */
    OP_LDI,    /* load indirect */
    OP_STI,    /* store indirect */
    OP_JMP,    /* jump */
    OP_RES,    /* reserved (unused) */
    OP_LEA,    /* load effective address */
    OP_TRAP    /* execute trap */
};

/* Memory-mapped special keyboard registers */

enum{
    MR_KBSR = 0xFE00,
    MR_KBDR = 0xFE02
};

/* TRAP Codes */
enum{
    TRAP_GETC = 0x20,  /* get a character from keyboard, not echoed onto terminal */
    TRAP_OUT = 0x21,   /* output a character */ 
    TRAP_PUTS = 0x22,  /* output a word string */
    TRAP_IN = 0x23,    /* get a character from keyboard, echoed onto the terminal */
    TRAP_PUTSP = 0x24, /* output a byte string */
    TRAP_HALT = 0x25,  /* halt the program */
};

/* memory storage (65536 locations) */
uint16_t memory[UINT16_MAX];
/* register storage */
uint16_t registers[R_COUNT];

uint16_t sign_extend(uint16_t x, int bit_count){
    if  ((x >> (bit_count - 1)) & 1){
        x |= (0xFFFF << bit_count);
    }
    return x;
}

uint16_t swap16(uint16_t x)
{
    return (x << 8) | (x >> 8);
}

void update_flags(uint16_t r){
    if (registers[r] == 0){
        registers[R_COND] = FL_ZRO;
    }
    else if (registers[r] >> 15) { /* a 1 in left-most bit indicates negative*/
        registers[R_COND] = FL_NEG;
    }else
    {
        registers[R_COND] = FL_POS;
    }
    
}

/* Read Image File */

void read_image_file(FILE* file){
    /* The origin tells where to place the image in memory */
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);

    uint16_t max_read  = UINT16_MAX - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    /* swap to little endian */
    while (read--  > 0){
        *p = swap16(*p);
        ++p;
    }
}

int read_image(const char* image_path){
    FILE* file = fopen(image_path, "rb");
    if(!file){
        return 0;
    };
    read_image_file(file);
    fclose(file);
    return 1;
}

uint16_t check_key()
{
    return WaitForSingleObject(hStdin, 1000) == WAIT_OBJECT_0 && _kbhit();
}

void mem_write(uint16_t address, uint16_t val){
    memory[address] = val;
}

uint16_t mem_read(uint16_t address){
    if(address == MR_KBSR){
        if(check_key()){
            memory[MR_KBSR] = (1 << 15);
            memory[MR_KBDR] = getchar();
        }
        else{
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}

/* Input buffering windows */
DWORD fdwMode, fdwOldMode;

void disable_input_buffering()
{
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hStdin, &fdwOldMode); /* save old mode */
    fdwMode = fdwOldMode 
            ^ ENABLE_ECHO_INPUT  /* no input echo */
            ^ ENABLE_LINE_INPUT; /* return when one or 
                                    more characters are available */
    SetConsoleMode(hStdin, fdwMode); /* set new mode */
    FlushConsoleInputBuffer(hStdin); /* clear buffer */
}

void restore_input_buffering()
{
    SetConsoleMode(hStdin, fdwOldMode);
}

void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

int main(int argc, char const *argv[])
{
    /* Load Arguments */
    if (argc < 2){
        /*show usage string*/
        printf("lc3 [image-file1] ...\n");
        exit(2);
    }

    for (int j = 1; j < argc; ++j){
        if (!read_image(argv[j])){
            printf("failed to load image: %s\n", argv[j]);
            exit(1);
        }
    }

    /* Setup */
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();
    //restore_input_buffering();

    /* Set the PC to starting position */
    /* 0x3000 is the default starting address */
    enum {
        PC_START = 0x3000
    };
    registers[R_PC] = PC_START;

    int running = 1;

    while(running){
        
        /* Fetch stage */
        uint16_t instr = mem_read(registers[R_PC]++);
        uint16_t op = instr >> 12;

        switch (op)
        {
        case OP_ADD:
                {
                    uint16_t r0 = (instr >> 9) & 0x7;

                    uint16_t r1 = (instr >> 6) & 0x7;

                    uint16_t imm_flag = (instr >> 5) & 0x1;

                    if (imm_flag){
                        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                        registers[r0] = registers[r1] + imm5;
                    }
                    else
                    {
                        uint16_t r2 = instr & 0x7;
                        registers[r0] = registers[r1] + registers[r2];
                    }
                    
                    update_flags(r0);
                }
                break;
            case OP_AND:
                {
                    uint16_t r0 = (instr >> 9) & 0x7;

                    uint16_t r1 = (instr >> 6) & 0x7;

                    uint16_t imm_flag = (instr >> 5) & 0x1;

                    if (imm_flag){
                        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                        registers[r0] = registers[r1] & imm5;
                    }
                    else
                    {
                        uint16_t r2 = instr & 0x7;
                        registers[r0] = registers[r1] & registers[r2];
                    }
                    
                    update_flags(r0);
                }
                break;
            case OP_NOT:
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t r1 = (instr >> 6) & 0x7;
                    
                    registers[r0] = ~registers[r1];
                    update_flags(r0);
                }
                break;
            case OP_BR:
                {
					uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);
					uint16_t cond_flag = (instr >> 9) && 0x7;
                    if(registers[R_COND] & cond_flag){
						registers[R_PC] = registers[R_PC] + pc_offset9;
					}

                }
                break;
            case OP_JMP:
				{
					uint16_t base_r = (instr >> 6) & 0x7;
					registers[R_PC] = registers[base_r];
				}
                break;
            case OP_JSR:
                {
					uint16_t long_flag = (instr >> 11) & 0x1;
					uint16_t base_r = (instr >> 6) & 0x7;
					uint16_t pc_offset11 = sign_extend(instr & 0x7FFF, 11);

					registers[R_R7] = registers[R_PC];

					if (long_flag == 0) {
						registers[R_PC] = registers[base_r]; /* JSRR */
					}
					else
					{
						registers[R_PC] = registers[R_PC] + pc_offset11; /* JSR */
					}
					
				}
                break;
            case OP_LD:
                {
					uint16_t r0 = (instr >> 9) & 0x7;
					uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);

					registers[r0] = mem_read(registers[R_PC] + pc_offset9);

					update_flags(r0);
				}
                break;
            case OP_LDI:
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t pc_offset9 = instr & 0x1FF;

                    registers[r0] = mem_read(registers[R_PC] + sign_extend(pc_offset9, 9));

                    update_flags(r0);
                }
                break;
            case OP_LDR:
                {
					uint16_t r0 = (instr >> 9) & 0x7;
					uint16_t base_r = (instr >> 6) & 0x7;
					uint16_t pc_offset6 = sign_extend(instr & 0x3F, 6);

					registers[r0] = mem_read(registers[base_r] + pc_offset6);

					update_flags(r0);
				}
                break;
            case OP_LEA:
                {
					uint16_t r0 = (instr >> 9) & 0x7;
					uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);

					registers[r0] = registers[R_PC] + pc_offset9;

					update_flags(r0);
				}
                break;
            case OP_ST:
                {
					uint16_t r0 = (instr >> 9) && 0x7;
					uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);

					mem_write(registers[R_PC] + pc_offset9, registers[r0]);
				}
                break;
            case OP_STI:
                {
					uint16_t r0 = (instr >> 9) & 0x7;
					uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);

					mem_write(mem_read(registers[R_PC] + pc_offset9), registers[r0]);
				}
                break;
            case OP_STR:
				{	
					uint16_t sr = (instr >> 9) & 0x7;
                    uint16_t base_r = (instr >> 9) & 0x7;
                    uint16_t pc_offset6 = sign_extend(instr & 0x3F, 9);

                    mem_write(registers[base_r] + pc_offset6, registers[sr]);
				}
                break;
            case OP_TRAP:
                {
                    //trap routines
                    switch (instr & 0xFF)
                    {
                    case TRAP_GETC:
                        {
                            registers[R_R0] = (uint16_t)getchar();
                        }
                        break;
                    case TRAP_OUT:
                        {
                            putc((char)registers[R_R0], stdout);
                            fflush(stdout);
                        }
                        break;
                    case TRAP_PUTS:
                        {
                            uint16_t* c = memory + registers[R_R0];
                            while (*c)
                            {
                                putc((char)*c, stdout);
                                ++c;
                            }
                            fflush(stdout);
                        }
                        break;
                    case TRAP_IN:
                        {
                            printf("Enter a character: ");
                            char c = getchar();
                            putc(c, stdout);
                            registers[R_R0] = (uint16_t)c;
                        }
                        break;
                    case TRAP_PUTSP:
                        {
                            
                        }
                    case TRAP_HALT:
                        {

                        }
                        break;
                    default:
                        break;
                    }

                }
                break;
            case OP_RES:
            case OP_RTI:
            default:
                {
                    // bad opcode
                }
                break;
        }

    }

    return 0;
}





