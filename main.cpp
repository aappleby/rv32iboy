#include "symlinks/CLI11/include/CLI/App.hpp"
#include "symlinks/CLI11/include/CLI/Config.hpp"
#include "symlinks/CLI11/include/CLI/Formatter.hpp"
#include "symlinks/metrolib/core/Platform.h"
#include "symlinks/metrolib/core/Tests.h"
#include "riscv_constants.h"

#include <stdio.h>


#define DONTCARE 0

struct GBState {
  union { uint16_t pc = 0; struct { uint8_t pcl; uint8_t pch; }; };
  union { uint16_t sp = 0; struct { uint8_t spl; uint8_t sph; }; };
  union { uint16_t xy = 0; struct { uint8_t xyl; uint8_t xyh; }; };

  union { uint16_t bc = 0; struct { uint8_t   c; uint8_t   b; }; };
  union { uint16_t de = 0; struct { uint8_t   e; uint8_t   d; }; };
  union { uint16_t hl = 0; struct { uint8_t   l; uint8_t   h; }; };
  union { uint16_t af = 0; struct { uint8_t   f; uint8_t   a; }; };
};

struct RVState {
  union {
    struct {
      uint32_t r00, r01, r02, r03, r04, r05, r06, r07;
      uint32_t r08, r09, r10, r11, r12, r13, r14, r15;
      uint32_t r16, r17, r18, r19, r20, r21, r22, r23;
      uint32_t r24, r25, r26, r27, r28, r29, r30, r31;
    };
    struct {
      uint32_t r[32];
    };
  };

  uint32_t pc;
  rv32_op  op;
  uint32_t imm;
  uint32_t raddr;
  uint32_t rdata;
  uint32_t waddr;
  uint32_t wdata;
  uint32_t wren;
  uint32_t wmask;
};

struct MemState {
  union {
    uint32_t u32[16384];
    uint8_t  u8 [65536];
  }
};

/*
struct tilelink_a {
  uint32_t a_opcode;
  uint32_t a_param;
  uint32_t a_size;
  bool     a_source;
  uint32_t a_address;
  uint32_t a_mask;
  uint32_t a_data;
  bool     a_valid;
  bool     a_ready; // reverse channel
};

//----------------------------------------

struct tilelink_d {
  uint32_t d_opcode;
  uint32_t d_param;
  uint32_t d_size;
  bool     d_source;
  uint32_t d_sink;
  uint32_t d_data;
  bool     d_error;
  bool     d_valid;
  bool     d_ready; // reverse channel
};
*/

GBState  gb;
RVState  rv;
MemState mem;

//----------------------------------------------------------------------------
// Translates a RV32I opcode into a TilelinkA transaction

/*
tilelink_a gen_bus(logic<7> op, uint32_t f3, uint32_t addr, uint32_t reg2) {
  tilelink_a tla;

  uint32_t bus_size = b3(DONTCARE);
  uint32_t mask_b   = 0;

  if (f3 == 0) { mask_b = 0b0001; bus_size = 0; }
  if (f3 == 1) { mask_b = 0b0011; bus_size = 1; }
  if (f3 == 2) { mask_b = 0b1111; bus_size = 2; }
  if (addr[0]) mask_b = mask_b << 1;
  if (addr[1]) mask_b = mask_b << 2;

  tla.a_address = addr;
  tla.a_data    = (reg2 << ((addr & 3) * 8));
  tla.a_mask    = mask_b;
  tla.a_opcode  = (op == RV32I::OP_STORE) ? (bus_size == 2 ? TL::PutFullData : TL::PutPartialData) : TL::Get;
  tla.a_param   = b3(DONTCARE);
  tla.a_size    = bus_size;
  tla.a_source  = b1(DONTCARE);
  tla.a_valid   = (op == RV32I::OP_LOAD) || (op == RV32I::OP_STORE);
  tla.a_ready   = 1;

  return tla;
}
*/

  //----------------------------------------------------------------------------

  uint32_t unpack_mem() {
    uint32_t mem = rv.rdata;

    if (rv.raddr & 0b01) mem = mem >> 8;
    if (rv.raddr & 0b10) mem = mem >> 16;

    switch (rv.op.f3) {
      case /*0*/ RV32I::F3_LB:  mem = int8_t(mem); break;
      case /*1*/ RV32I::F3_LH:  mem = int16_t(mem); break;
      case /*2*/ RV32I::F3_LW:  mem = mem; break;
      case /*3*/ RV32I::F3_LD:  mem = mem; break;
      case /*4*/ RV32I::F3_LBU: mem = uint8_t(mem); break;
      case /*5*/ RV32I::F3_LHU: mem = uint16_t(mem); break;
      case /*6*/ RV32I::F3_LWU: mem = mem; break;
      case /*7*/ RV32I::F3_LDU: mem = mem; break;
    }

    return mem;
  }

  //----------------------------------------------------------------------------

  bool take_branch(uint32_t reg1, uint32_t reg2) {
    bool eq  = reg1 == reg2;
    bool slt = signed(reg1) < signed(reg2);
    bool ult = reg1 < reg2;

    bool result;
    switch (rv.op.f3) {
      case /*0*/ RV32I::F3_BEQ:  result =   eq; break;
      case /*1*/ RV32I::F3_BNE:  result =  !eq; break;
      case /*2*/ RV32I::F3_BEQU: result =   eq; break;
      case /*3*/ RV32I::F3_BNEU: result =  !eq; break;
      case /*4*/ RV32I::F3_BLT:  result =  slt; break;
      case /*5*/ RV32I::F3_BGE:  result = !slt; break;
      case /*6*/ RV32I::F3_BLTU: result =  ult; break;
      case /*7*/ RV32I::F3_BGEU: result = !ult; break;
      default:             result = DONTCARE; break;
    }

    return result;
  }

//----------------------------------------------------------------------------

void decode_imm() {
  uint32_t imm_i = int32_t(rv.op.i.imm_11_0);
  uint32_t imm_s = int32_t((rv.op.s.imm_11_5 << 5) | rv.op.s.imm_4_0);
  uint32_t imm_u = rv.op.u.imm_31_12 << 12;

  uint32_t imm_b = 0;
  imm_b |= (rv.op.b.imm_12   & 0b000001) << 12;
  imm_b |= (rv.op.b.imm_11   & 0b000001) << 11;
  imm_b |= (rv.op.b.imm_10_5 & 0b111111) << 5;
  imm_b |= (rv.op.b.imm_4_1  & 0b001111) << 1;

  uint32_t imm_j = 0;
  imm_j |= (rv.op.j.imm_20    & 0b0000000001) << 20;
  imm_j |= (rv.op.j.imm_19_12 & 0b0011111111) << 12;
  imm_j |= (rv.op.j.imm_11    & 0b0000000001) << 11;
  imm_j |= (rv.op.j.imm_10_1  & 0b1111111111) << 1;

  uint32_t result;

  switch(rv.op.op) {
    case RV32I::OP_LOAD:   result = imm_i; break;
    case RV32I::OP_OPIMM:  result = imm_i; break;
    case RV32I::OP_AUIPC:  result = imm_u; break;
    case RV32I::OP_STORE:  result = imm_s; break;
    case RV32I::OP_OP:     result = imm_i; break;
    case RV32I::OP_LUI:    result = imm_u; break;
    case RV32I::OP_BRANCH: result = imm_b; break;
    case RV32I::OP_JALR:   result = imm_i; break;
    case RV32I::OP_JAL:    result = imm_j; break;
    default:               result = DONTCARE; break;
  }

  rv.imm = result;
}

//----------------------------------------------------------------------------

uint32_t execute_alu(uint32_t alu1, uint32_t alu2) {
  uint32_t result;

  switch (rv.op.f3) {
    case RV32I::F3_ADDSUB: result = (rv.op.op == RV32I::OP_OP && rv.op.f7 == 32) ? alu1 - alu2 : alu1 + alu2; break;
    case RV32I::F3_SL:     result = alu1 << (alu2 & 0b11111); break;
    case RV32I::F3_SLT:    result = signed(alu1) < signed(alu2); break;
    case RV32I::F3_SLTU:   result = alu1 < alu2; break;
    case RV32I::F3_XOR:    result = alu1 ^ alu2; break;
    case RV32I::F3_SR:     result = rv.op.f7 == 32 ? signed(alu1) >> (alu2 & 0b11111) : alu1 >> (alu2 & 0b11111); break;
    case RV32I::F3_OR:     result = alu1 | alu2; break;
    case RV32I::F3_AND:    result = alu1 & alu2; break;
    default:               result = DONTCARE; break;
  }

  return result;
}

//----------------------------------------------------------------------------

uint32_t next_pc(uint32_t reg1, uint32_t reg2) {

  uint32_t new_pc;
  switch(rv.op.op) {
    case RV32I::OP_BRANCH: new_pc = take_branch(reg1, reg2) ? rv.pc + rv.imm : rv.pc + 4; break;
    case RV32I::OP_JAL:    new_pc = rv.pc + rv.imm; break;
    case RV32I::OP_JALR:   new_pc = reg1  + rv.imm; break;
    case RV32I::OP_LUI:    new_pc = rv.pc + 4;      break;
    case RV32I::OP_AUIPC:  new_pc = rv.pc + 4;      break;
    case RV32I::OP_LOAD:   new_pc = rv.pc + 4;      break;
    case RV32I::OP_STORE:  new_pc = rv.pc + 4;      break;
    case RV32I::OP_SYSTEM: new_pc = rv.pc + 4;      break;
    case RV32I::OP_OPIMM:  new_pc = rv.pc + 4;      break;
    case RV32I::OP_OP:     new_pc = rv.pc + 4;      break;
    default:               new_pc = DONTCARE;       break;
  }

  return new_pc;
}

//----------------------------------------------------------------------------

void execute() {
  uint32_t result;

  decode_imm();
  uint32_t reg1 = rv.r[rv.op.rs1];
  uint32_t reg2 = rv.r[rv.op.rs2];

  switch(rv.op.op) {
    case RV32I::OP_OPIMM:  result = execute_alu(reg1, rv.imm); break;
    case RV32I::OP_OP:     result = execute_alu(reg1, reg2); break;
    case RV32I::OP_SYSTEM: result = DONTCARE; break;
    case RV32I::OP_BRANCH: result = DONTCARE; break;
    case RV32I::OP_JAL:    result = rv.pc + 4; break;
    case RV32I::OP_JALR:   result = rv.pc + 4; break;
    case RV32I::OP_LUI:    result = rv.imm; break;
    case RV32I::OP_AUIPC:  result = rv.pc + rv.imm; break;
    case RV32I::OP_LOAD:   result = DONTCARE; break;
    case RV32I::OP_STORE:  result = reg2; break;
    default:               result = DONTCARE; break;
  }

  if (rv.op.rd) rv.r[rv.op.rd] = result;
}

//----------------------------------------------------------------------------

void tock() {
  #if 0

  uint32_t BC_addr = b32(BC_reg1 + BC_imm);

  //----------------------------------------

  bool CD_read_mem  = C_insn_.r.op == RV32I::OP_LOAD;
  bool CD_write_mem = C_insn_.r.op == RV32I::OP_STORE;

  uint32_t BC_hpc = next_pc(B_hpc_, B_insn_, BC_reg1, BC_reg2);

  uint32_t BC_result = execute(B_hpc_, B_insn_, BC_reg1, BC_reg2, BC_hpc);

  //----------------------------------------

  uint32_t CD_writeback;

  if (CD_read_mem)     CD_writeback = unpack_mem(C_insn_.r.f3, C_addr_, data_tld.d_data);
  else if (C_hpc_.active)   CD_writeback = C_result_;

  //----------------------------------------
  // Regfile write

  CD_waddr = b5(C_insn_.r.rd);
  CD_wdata = CD_writeback;
  CD_wren  = C_insn_.r.rd && C_insn_.r.op != RV32I::OP_STORE && C_insn_.r.op != RV32I::OP_BRANCH;

  // FIXME move to phase C
  data_tla = gen_bus(B_insn_.r.op, B_insn_.r.f3, BC_addr, BC_reg2);

  if (reset_in) {
    A_hpc_ = 0;
    C_addr_     = 0;
    C_result_   = 0;
    rv.waddr = 0;
    D_wdata_ = 0;
    D_wren_  = 0;
  }

#endif
}

//----------------------------------------------------------------------------





//------------------------------------------------------------------------------

const char* instructions[38] = {
  "add", "addi", "and", "andi", "auipc", "beq",  "bge", "bgeu",
  "blt", "bltu", "bne", "jal",  "jalr",  "lb",   "lbu", "lh",
  "lhu", "lui",  "lw",  "or",   "ori",   "sb",   "sh",  "simple",
  "sll", "slli", "slt", "slti", "sltiu", "sltu", "sra", "srai",
  "srl", "srli", "sub", "sw",   "xor",   "xori"
};

//------------------------------------------------------------------------------

double total_tocks = 0;
double total_time = 0;



TestResults test_instruction(
  std::string& inst_path,
  const char* test_name,
  const int reps,
  const int max_cycles
) {
  TEST_INIT("Testing op %6s, %d reps", test_name, reps);

  char code_filename[256];
  char data_filename[256];
  sprintf(code_filename, "%s/%s.code.vh", inst_path.c_str(), test_name);
  sprintf(data_filename, "%s/%s.data.vh", inst_path.c_str(), test_name);

  int elapsed_cycles = 0;
  int test_result = -1;

  //----------

  toplevel top(code_filename, data_filename);

  for (int rep = 0; rep < reps; rep++) {
    top.reset = 1;
    top.tock(0);
    top.reset = 0;

    auto time_a = timestamp();
    for (elapsed_cycles = 0; elapsed_cycles < max_cycles; elapsed_cycles++) {
      top.tock(0);
      total_tocks++;
      if (top.bus_address == 0xfffffff0 && top.bus_write_enable) {
        test_result = top.bus_write_data;
        break;
      }
    }
    auto time_b = timestamp();
    total_time += time_b - time_a;

  }

  //----------


  EXPECT_NE(max_cycles, elapsed_cycles, "TIMEOUT");
  EXPECT_NE(0, test_result, "FAIL %d @ %d\n", test_result, time);
  TEST_DONE();
}

//------------------------------------------------------------------------------

int main(int argc, const char** argv) {
  CLI::App app{"Simple test and benchmark for rvsimple"};

  std::string inst_path;
  int reps = 1;
  int max_cycles = 1000;

  //"tests/risc-v/instructions/build/benchmark.code.vh",

  app.add_option("inst_path", inst_path, "Path to the compiled instructions");
  app.add_option("-r,--reps", reps, "How many times to repeat the test");
  app.add_option("-m,--max_cycles", max_cycles,
                 "Maximum # cycles to simulate before timeout");
  CLI11_PARSE(app, argc, argv);

  LOG_B("Starting %s @ %d reps...\n", argv[0], reps);

  total_tocks = 0;
  total_time = 0;

  LOG_B("Testing...\n");
  TestResults results;
  for (int i = 0; i < 38; i++) {
    results << test_instruction(inst_path, instructions[i], reps, max_cycles);
  }

  double rate = double(total_tocks) / double(total_time);
  LOG_B("Total tocks %f\n",  total_tocks);
  LOG_B("Total time %f\n",   total_time);
  LOG_B("Sim rate %f mhz\n", rate / 1.0e6);

  return results.show_result();
}

//----------------------------------------------------------------------------
