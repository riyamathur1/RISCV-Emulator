#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>


#include "rv_emu.h"
#include "bits.h"

#define DEBUG 0

static void unsupported(char *s, uint32_t n) {
    printf("unsupported %s 0x%x\n", s, n);
    exit(-1);
}


static void emu_r_type(rv_state *state, uint32_t iw);
static void emu_i_type(rv_state *state, uint32_t iw);
static void emu_b_type(rv_state *state, uint32_t iw);
static void emu_jal(rv_state *state, uint32_t iw);
static void emu_jalr(rv_state *state, uint32_t iw);
static void emu_st_type(rv_state *state, uint32_t iw);
static void emu_ld_type(rv_state *state, uint32_t iw);
static int32_t sign_extension(uint32_t value, int bit_count);


static void rv_one(rv_state *state) {
    uint32_t iw  = *((uint32_t*) state->pc);
    iw = cache_lookup(&state->i_cache, (uint64_t) state->pc);
    uint32_t opcode = get_bits(iw, 0, 7);

    state->analysis.i_count++;


#if DEBUG
    printf("iw: %x\n", iw);
#endif
	
    switch (opcode) {
        case 0b0110011: // R-type
            emu_r_type(state, iw);
            state->analysis.ir_count++;
            break;
        case 0b0010011: // I-type
            emu_i_type(state, iw);
            state->analysis.ir_count++;
            break;
        case 0b0000011: // Loads
        	emu_ld_type(state, iw);
            state->analysis.ld_count++;     
            break;
        case 0b0100011: // Stores
        	emu_st_type(state, iw);
            state->analysis.st_count++;          
            break;
        case 0b1100011: // B-type
            emu_b_type(state, iw);       
            break;
        case 0b1101111: // JAL
            emu_jal(state, iw);
            state->analysis.j_count++;
            break;
        case 0b1100111: // JALR
            emu_jalr(state, iw);
            state->analysis.j_count++;
            break;
        default:
            unsupported("Unknown opcode: ", opcode);
            break;
    }
}

void rv_init(rv_state *state, uint32_t *target, 
             uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    state->pc = (uint64_t) target;
    state->regs[RV_A0] = a0;
    state->regs[RV_A1] = a1;
    state->regs[RV_A2] = a2;
    state->regs[RV_A3] = a3;

    state->regs[RV_ZERO] = 0;  // zero is always 0  (:
    state->regs[RV_RA] = RV_STOP;
    state->regs[RV_SP] = (uint64_t) &state->stack[STACK_SIZE];

    memset(&state->analysis, 0, sizeof(rv_analysis));
    cache_init(&state->i_cache);
}


uint64_t rv_emulate(rv_state *state) {
    while (state->pc != RV_STOP) {
        rv_one(state);
    }
    return state->regs[RV_A0];
}

static void print_pct(char *fmt, int numer, int denom) {
    double pct = 0.0;

    if (denom)
        pct = (double) numer / (double) denom * 100.0;
    printf(fmt, numer, pct);
}

void rv_print(rv_analysis *a) {
    int b_total = a->b_taken + a->b_not_taken;

    printf("=== Analysis\n");
    print_pct("Instructions Executed  = %d\n", a->i_count, a->i_count);
    print_pct("R-type + I-type        = %d (%.2f%%)\n", a->ir_count, a->i_count);
    print_pct("Loads                  = %d (%.2f%%)\n", a->ld_count, a->i_count);
    print_pct("Stores                 = %d (%.2f%%)\n", a->st_count, a->i_count);    
    print_pct("Jumps/JAL/JALR         = %d (%.2f%%)\n", a->j_count, a->i_count);
    print_pct("Conditional branches   = %d (%.2f%%)\n", b_total, a->i_count);
    print_pct("  Branches taken       = %d (%.2f%%)\n", a->b_taken, b_total);
    print_pct("  Branches not taken   = %d (%.2f%%)\n", a->b_not_taken, b_total);
}


void emu_r_type(rv_state *rsp, uint32_t iw) {
    uint32_t rd = (iw >> 7) & 0b11111;
    uint32_t rs1 = (iw >> 15) & 0b11111;
    uint32_t rs2 = (iw >> 20) & 0b11111;
    uint32_t funct3 = (iw >> 12) & 0b111;
    uint32_t funct7 = (iw >> 25) & 0b1111111;

	// ADD
    if (funct3 == 0b000 && funct7 == 0b0000000) {
        rsp->regs[rd] = rsp->regs[rs1] + rsp->regs[rs2];
    }
    // SUB
    else if (funct3 == 0b000 && funct7 == 0b0100000) {
    	rsp->regs[rd] = rsp->regs[rs1] - rsp->regs[rs2];
    }
    // MUL
    else if (funct3 == 0b000 && funct7 == 0b0000001) {
    	rsp->regs[rd] = rsp->regs[rs1] * rsp->regs[rs2];
   	}
    // DIV
    else if (funct3 == 0b100 && funct7 == 0b0000001) {
    	rsp->regs[rd] = rsp->regs[rs1] / rsp->regs[rs2];
    }
    // SRL
    else if (funct3 == 0b101 && funct7 == 0b0000000) {
        rsp->regs[rd] = rsp->regs[rs1] >> (rsp->regs[rs2] & 0b111111);
	// SLL
    } else if (funct3 == 0b001 && funct7 == 0b0000000) {
        rsp->regs[rd] = rsp->regs[rs1] << (rsp->regs[rs2] & 0b111111); 
    // AND
    } else if (funct3 == 0b111 && funct7 == 0b0000000) {
            rsp->regs[rd] = rsp->regs[rs1] & rsp->regs[rs2];
   	// REM
	} else if (funct3 == 0b110 && funct7 == 0b0000001) {
        if (rsp->regs[rs2] != 0) {  // checking to avoid division by zero
            rsp->regs[rd] = (int64_t)(rsp->regs[rs1]) % (int64_t)(rsp->regs[rs2]);
        } else {
            printf("Division by zero in REM instruction\n");
            exit(-1);
        }
    } else {
        unsupported("R-type funct3", funct3);
    }

    rsp->pc += 4; // Next instruction
}

void emu_i_type(rv_state *rsp, uint32_t iw) {
    uint32_t opcode = (iw & 0b1111111);
    uint32_t rd = (iw >> 7) & 0b11111;
    uint32_t rs1 = (iw >> 15) & 0b11111;
    int32_t imm = sign_extension(iw >> 20, 12); // immediate value, sign-extended
    uint32_t funct3 = (iw >> 12) & 0b111;

    switch (opcode) {
        case 0b0010011: // Arithmetic/logical instructions
            switch (funct3) {
                case 0b001: // SLLI
                    rsp->regs[rd] = rsp->regs[rs1] << (imm & 0b11111);
                    break;
                case 0b101: // SRLI
                    rsp->regs[rd] = rsp->regs[rs1] >> (imm & 0b11111);
                    break;
                case 0b010: // LI
                    rsp->regs[rd] = imm;
                    break;
                case 0b000: // ADDI
                    rsp->regs[rd] = rsp->regs[rs1] + imm;
                    break;
                default:
                    unsupported("Unsupported I-type funct3", funct3);
                    break;
            }
            break;
        default:
            unsupported("Unknown I-type opcode", opcode);
            break;
    }

    rsp->pc += 4; // Next instruction
}


// function to sign extend a value to a specified number of bits
int32_t sign_extension(uint32_t value, int bit_count) {
	// checking if the most significant bit is set
	if (value & (1 << (bit_count - 1))) {
		// if set, extend sign by OR-ing with a mask of 1s
		return (int32_t)(value | (~((1 << bit_count) - 1)));
	} else {
		// if not set, return the value as is
		return (int32_t)value;
	}
}

void emu_b_type(rv_state *rsp, uint32_t iw) {
    uint32_t opcode = (iw & 0b1111111);
    uint32_t rs1 = (iw >> 15) & 0b11111;
    uint32_t rs2 = (iw >> 20) & 0b11111;

    // Bits [11:8]
    uint32_t imm1 = (iw >> 8) & 0b1111;
    // Bits [30:25] 
    uint32_t imm2 = (iw >> 25) & 0b1111;
    // 7th bit
    uint32_t imm3 = (iw >> 7) & 0b1;
    // 11th bit
    uint32_t imm4 = (iw >> 31) & 0b1;

    // calculating the offset by combining immediate values and sign-extending
    uint32_t offset = sign_extension((imm4 << 12) | (imm3 << 11) | (imm2 << 5) | (imm1 << 1), 12);

    // branch instructions
    if (opcode == 0b1100011) {
        uint32_t funct3 = (iw >> 12) & 0b111;

        // performing comparison based on funct3
        int64_t compare_result;
        switch (funct3) {
            // BEQ
            case 0b000:
                if (rsp->regs[rs1] == rsp->regs[rs2]) {
                    rsp->pc += offset; // Branch taken
                    rsp->analysis.b_taken++;
                   
                } else {
                    rsp->pc += 4; // Next instruction
                    rsp->analysis.b_not_taken++;
                }
                break;
            // BNE
            case 0b001:
                if (rsp->regs[rs1] != rsp->regs[rs2]) {
                    rsp->pc += offset;
                    rsp->analysis.b_taken++;
                } else {
                    rsp->pc += 4;
                    rsp->analysis.b_not_taken++;
                }
                break;
            // BLT
            case 0b100:
                compare_result = (int64_t)rsp->regs[rs1] - (int64_t)rsp->regs[rs2];
                if (compare_result < 0) {
                    rsp->pc += offset;
                    rsp->analysis.b_taken++;
                } else {
                    rsp->pc += 4;
                    rsp->analysis.b_not_taken++;
                }
                break;
            // BGE
            case 0b101:
                compare_result = (int64_t)rsp->regs[rs1] - (int64_t)rsp->regs[rs2];
                if (compare_result >= 0) {
                    rsp->pc += offset;
                    rsp->analysis.b_taken++;
                } else {
                    rsp->pc += 4;
                    rsp->analysis.b_not_taken++;
                }
                break;
            default:
                unsupported("Unsupported B-type funct3", funct3);
        }
    } else {
        unsupported("Unknown B-type opcode", opcode);
    }
}

void emu_ld_type(rv_state *rsp, uint32_t iw) {
	uint32_t rd = (iw >> 7) & 0b11111;
	uint32_t rs1 = (iw >> 15) & 0b11111;
	uint32_t funct3 = (iw >> 12) & 0b111;

	uint32_t imm = get_bits(iw, 20, 12);
	uint64_t offset = sign_extend(imm, 12);
	int64_t address = (int64_t)rsp->regs[rs1];


	// LB
	if (funct3 == 0b000) {
		uint8_t *value = (uint8_t*)(address + offset);
		rsp->regs[rd] = *value;
	} else if (funct3 == 0b010) {	// LW
		uint32_t *value = (uint32_t*)(address + offset);
		rsp->regs[rd] = *value;
	} else if (funct3 == 0b011) { 	// LD
		uint64_t *value = (uint64_t*)(address + offset);
		rsp->regs[rd] = *value;		
	} else {
		unsupported("Unknown L-type funct3", funct3);
	}
	
    rsp->pc += 4; // next instruction
}


void emu_st_type(rv_state *rsp, uint32_t iw) {
    uint32_t funct3 = (iw >> 12) & 0b111;
    uint32_t rs1 = (iw >> 15) & 0b11111;
    uint32_t rs2 = (iw >> 20) & 0b11111;
    int32_t imm = ((iw >> 25) << 5) | ((iw >> 7) & 0b11111);
    imm = sign_extension(imm, 12); // sign extending the immediate to 32 bits

    uint64_t address = rsp->regs[rs1] + imm; // calculate effective address using base register and immediate

    switch (funct3) {
        case 0b000: // SB
            *(uint8_t *)(address) = rsp->regs[rs2] & 0xFF;
            break;
        case 0b010: // SW
            *(uint32_t *)(address) = rsp->regs[rs2] & 0xFFFFFFFF;
            break;
        case 0b011: // SD
            *(uint64_t *)(address) = rsp->regs[rs2];
            break;
        default:
            unsupported("Unsupported S-type funct3", funct3);
            break;
    }

    rsp->pc += 4; // next instruction
}


void emu_jal(rv_state *rsp, uint32_t iw) {
    // extracting rd
    uint32_t rd = (iw >> 7) & 0x1F;

    // extracting imm value bits
    int32_t imm = ((iw >> 31) << 20)  // [20]
                | ((iw >> 21) & 0x3FF) << 1  // [10:1]
                | ((iw >> 20) & 1) << 11  // [11]
                | ((iw >> 12) & 0xFF) << 12; // [19:12]
    
    // imm value, sign extended
    imm = sign_extension(imm, 21);

    // calculating the target address and align it to a 4-byte boundary
    uint64_t target_address = rsp->pc + imm;

    // write address of the next instruction if it's not x0
    if (rd != 0) {
        rsp->regs[rd] = rsp->pc + 4;
    }

    // updating pc to the target address
    rsp->pc = target_address;
}


void emu_jalr(rv_state *rsp, uint32_t iw) {
    uint32_t rs1 = (iw >> 15) & 0b11111;  // Will be ra (aka x1)
    uint64_t val = rsp->regs[rs1];  // Value of regs[1]

    rsp->pc = val;  // PC = return address
}



