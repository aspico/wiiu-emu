#include "bitutils.h"
#include "floatutils.h"
#include "interpreter.h"

static std::pair<uint32_t, uint32_t>
getCRFRange(uint32_t field)
{
   auto me = ((8 - field) * 4) - 1;
   auto mb = me - 3;
   return { mb, me };
}

uint32_t
getCRF(ThreadState *state, uint32_t field)
{
   auto bits = getCRFRange(field);
   return (state->cr.value >> bits.first) & 0xf;
}

void
setCRF(ThreadState *state, uint32_t field, uint32_t value)
{
   auto cr = state->cr.value;
   auto bits = getCRFRange(field);
   auto mask = make_bitmask<uint32_t>(bits.first, bits.second);

   cr = (cr & ~mask) | (value << bits.first);
   state->cr.value = cr;
}

uint32_t
getCRB(ThreadState *state, uint32_t bit)
{
   return get_bit(state->cr.value, 31 - bit);
}

void
setCRB(ThreadState *state, uint32_t bit, uint32_t value)
{
   state->cr.value = set_bit_value(state->cr.value, 31 - bit, value);
}

// Compare
enum CmpFlags
{
   CmpImmediate = 1 << 0, // b = imm
};

template<typename Type, unsigned flags = 0>
static void
cmpGeneric(ThreadState *state, Instruction instr)
{
   Type a, b;
   uint32_t c;

   a = state->gpr[instr.rA];

   if (flags & CmpImmediate) {
      if (std::is_signed<Type>::value) {
         b = sign_extend<16>(instr.simm);
      } else {
         b = instr.uimm;
      }
   } else {
      b = state->gpr[instr.rB];
   }

   if (a < b) {
      c = ConditionRegisterFlag::LessThan;
   } else if (a > b) {
      c = ConditionRegisterFlag::GreaterThan;
   } else {
      c = ConditionRegisterFlag::Equal;
   }

   if (state->xer.so) {
      c |= ConditionRegisterFlag::SummaryOverflow;
   }

   setCRF(state, instr.crfD, c);
}

static void
cmp(ThreadState *state, Instruction instr)
{
   return cmpGeneric<int32_t>(state, instr);
}

static void
cmpi(ThreadState *state, Instruction instr)
{
   return cmpGeneric<int32_t, CmpImmediate>(state, instr);
}

static void
cmpl(ThreadState *state, Instruction instr)
{
   return cmpGeneric<uint32_t>(state, instr);
}

static void
cmpli(ThreadState *state, Instruction instr)
{
   return cmpGeneric<uint32_t, CmpImmediate>(state, instr);
}

// Floating Compare
enum FCmpFlags
{
   FCmpOrdered    = 1 << 0,
   FCmpUnordered  = 1 << 1,
   FCmpSingle0    = 1 << 2,
   FCmpSingle1    = 1 << 3,
};

template<typename Type, unsigned flags>
static void
fcmpGeneric(ThreadState *state, Instruction instr)
{
   Type a, b;
   uint32_t c;
   bool vxsnan = false;

   if (flags & FCmpSingle0) {
      a = static_cast<Type>(state->fpr[instr.frA].paired0);
      b = static_cast<Type>(state->fpr[instr.frB].paired0);
   } else if (flags & FCmpSingle1) {
      a = static_cast<Type>(state->fpr[instr.frA].paired1);
      b = static_cast<Type>(state->fpr[instr.frB].paired1);
   } else {
      a = static_cast<Type>(state->fpr[instr.frA].value);
      b = static_cast<Type>(state->fpr[instr.frB].value);
   }

   vxsnan = (is_signalling_nan(a) || is_signalling_nan(b));

   if (a < b) {
      c = ConditionRegisterFlag::LessThan;
   } else if (a > b) {
      c = ConditionRegisterFlag::GreaterThan;
   } else if (a == b) {
      c = ConditionRegisterFlag::Equal;
   } else {
      c = ConditionRegisterFlag::Unordered;

      if (is_signalling_nan(a) || is_signalling_nan(b)) {
         state->fpscr.vxsnan = 1;

         if ((flags & FCmpOrdered) && state->fpscr.ve) {
            state->fpscr.vxvc = 1;
         }
      } else if ((flags & FCmpOrdered)) {
         state->fpscr.vxvc = 1;
      }
   }

   setCRF(state, instr.crfD, c);
   state->fpscr.cr1 = c;
   state->fpscr.vx |= state->fpscr.vxvc;
   state->fpscr.fx |= state->fpscr.vx;
}

static void
fcmpo(ThreadState *state, Instruction instr)
{
   return fcmpGeneric<double, FCmpOrdered>(state, instr);
}

static void
fcmpu(ThreadState *state, Instruction instr)
{
   return fcmpGeneric<double, FCmpUnordered>(state, instr);
}

static void
ps_cmpo0(ThreadState *state, Instruction instr)
{
   return fcmpGeneric<float, FCmpOrdered | FCmpSingle0>(state, instr);
}

static void
ps_cmpo1(ThreadState *state, Instruction instr)
{
   return fcmpGeneric<float, FCmpOrdered | FCmpSingle1>(state, instr);
}

static void
ps_cmpu0(ThreadState *state, Instruction instr)
{
   return fcmpGeneric<float, FCmpUnordered | FCmpSingle0>(state, instr);
}

static void
ps_cmpu1(ThreadState *state, Instruction instr)
{
   return fcmpGeneric<float, FCmpUnordered | FCmpSingle1>(state, instr);
}

// Condition Register AND
static void
crand(ThreadState *state, Instruction instr)
{
   uint32_t a, b, d;
   a = getCRB(state, instr.crbA);
   b = getCRB(state, instr.crbB);

   d = a & b;
   setCRB(state, instr.crbD, d);
}

// Condition Register AND with Complement
static void
crandc(ThreadState *state, Instruction instr)
{
   uint32_t a, b, d;
   a = getCRB(state, instr.crbA);
   b = getCRB(state, instr.crbB);

   d = a & ~b;
   setCRB(state, instr.crbD, d);
}

// Condition Register Equivalent
static void
creqv(ThreadState *state, Instruction instr)
{
   uint32_t a, b, d;
   a = getCRB(state, instr.crbA);
   b = getCRB(state, instr.crbB);

   d = ~(a ^ b);
   setCRB(state, instr.crbD, d);
}

// Condition Register NAND
static void
crnand(ThreadState *state, Instruction instr)
{
   uint32_t a, b, d;
   a = getCRB(state, instr.crbA);
   b = getCRB(state, instr.crbB);

   d = ~(a & b);
   setCRB(state, instr.crbD, d);
}

// Condition Register NOR
static void
crnor(ThreadState *state, Instruction instr)
{
   uint32_t a, b, d;
   a = getCRB(state, instr.crbA);
   b = getCRB(state, instr.crbB);

   d = ~(a | b);
   setCRB(state, instr.crbD, d);
}

// Condition Register OR
static void
cror(ThreadState *state, Instruction instr)
{
   uint32_t a, b, d;
   a = getCRB(state, instr.crbA);
   b = getCRB(state, instr.crbB);

   d = a | b;
   setCRB(state, instr.crbD, d);
}

// Condition Register OR with Complement
static void
crorc(ThreadState *state, Instruction instr)
{
   uint32_t a, b, d;
   a = getCRB(state, instr.crbA);
   b = getCRB(state, instr.crbB);

   d = a | ~b;
   setCRB(state, instr.crbD, d);
}

// Condition Register XOR
static void
crxor(ThreadState *state, Instruction instr)
{
   uint32_t a, b, d;
   a = getCRB(state, instr.crbA);
   b = getCRB(state, instr.crbB);

   d = a ^ b;
   setCRB(state, instr.crbD, d);
}

// Move Condition Register Field
static void
mcrf(ThreadState *state, Instruction instr)
{
   setCRF(state, instr.crfD, getCRF(state, instr.crfS));
}

// Move to Condition Register from XER
static void
mcrxr(ThreadState *state, Instruction instr)
{
   setCRF(state, instr.crfD, state->xer.crxr);
   state->xer.crxr = 0;
}

// Move from Condition Register
static void
mfcr(ThreadState *state, Instruction instr)
{
   state->gpr[instr.rD] = state->cr.value;
}

// Move to Condition Register Fields
static void
mtcrf(ThreadState *state, Instruction instr)
{
   uint32_t cr, crm, s, mask;

   s = state->gpr[instr.rS];
   cr = state->cr.value;
   crm = instr.crm;
   mask = 0;

   for (auto i = 0u; i < 8; ++i) {
      if (crm & (1 << i)) {
         mask |= 0xf << (i * 4);
      }
   }

   cr = (s & mask) | (cr & ~mask);
   state->cr.value = cr;
}

void
Interpreter::registerConditionInstructions()
{
   RegisterInstruction(cmp);
   RegisterInstruction(cmpi);
   RegisterInstruction(cmpl);
   RegisterInstruction(cmpli);
   RegisterInstruction(fcmpo);
   RegisterInstruction(fcmpu);
   RegisterInstruction(crand);
   RegisterInstruction(crandc);
   RegisterInstruction(creqv);
   RegisterInstruction(crnand);
   RegisterInstruction(crnor);
   RegisterInstruction(cror);
   RegisterInstruction(crorc);
   RegisterInstruction(crxor);
   RegisterInstruction(mcrf);
   RegisterInstruction(mcrxr);
   RegisterInstruction(mfcr);
   RegisterInstruction(mtcrf);
   RegisterInstruction(ps_cmpu0);
   RegisterInstruction(ps_cmpo0);
   RegisterInstruction(ps_cmpu1);
   RegisterInstruction(ps_cmpo1);
}
