// wgsl_test.cpp -- native test harness for the SPIR-V to WGSL translator.
// Hand-assembles a fragment-shader SPIR-V module (uniform block, sampled
// image, loop, if/else with merge, bitfield op, GLSL.std.450 extinst,
// locations/builtins), runs TranslateSpirvToWgsl, prints the WGSL and runs
// structural checks.
#include "wgsl_translator.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using rex::graphics::webgpu::TranslateSpirvToWgsl;
using rex::graphics::webgpu::WgslTranslationResult;

namespace {

enum : uint32_t {
  kOpExtInstImport = 11, kOpEntryPoint = 15, kOpExecutionMode = 16,
  kOpCapability = 17, kOpTypeVoid = 19, kOpTypeBool = 20, kOpTypeInt = 21,
  kOpTypeFloat = 22, kOpTypeVector = 23, kOpTypeImage = 25, kOpTypeSampler = 26,
  kOpTypeSampledImage = 27, kOpTypeStruct = 30, kOpTypePointer = 32,
  kOpTypeFunction = 33, kOpConstant = 43, kOpConstantComposite = 44,
  kOpFunction = 54, kOpFunctionParameter = 55, kOpFunctionEnd = 56,
  kOpVariable = 59, kOpLoad = 61, kOpStore = 62, kOpAccessChain = 65,
  kOpDecorate = 67, kOpMemberName = 6, kOpName = 5, kOpExtInst = 12,
  kOpImageSampleImplicitLod = 87, kOpSampledImage = 86, kOpIAdd = 128,
  kOpFAdd = 129, kOpFMul = 133, kOpFDiv = 136, kOpVectorTimesScalar = 142,
  kOpFOrdGreaterThan = 186, kOpBitFieldUExtract = 203, kOpDPdx = 207,
  kOpPhi = 245, kOpLoopMerge = 246, kOpSelectionMerge = 247, kOpLabel = 248,
  kOpBranch = 249, kOpBranchConditional = 250, kOpReturn = 253,
  kOpReturnValue = 254, kOpSLessThan = 177,
};
constexpr uint32_t kSCInput = 1, kSCUniform = 2, kSCOutput = 3,
                    kSCUniformConstant = 0, kSCFunction = 7;
constexpr uint32_t kDecBlock = 2, kDecBuiltIn = 11, kDecLocation = 30,
                    kDecBinding = 33, kDecDescriptorSet = 34;
constexpr uint32_t kBiFrontFacing = 17, kBiFragCoord = 15;

std::vector<uint32_t> StrWords(const char* s) {
  std::vector<uint32_t> w;
  uint32_t cur = 0;
  int shift = 0;
  for (const char* p = s;; ++p) {
    cur |= static_cast<uint32_t>(static_cast<unsigned char>(*p)) << shift;
    shift += 8;
    if (shift == 32) {
      w.push_back(cur);
      cur = 0;
      shift = 0;
    }
    if (*p == '\0') break;
  }
  if (shift > 0) w.push_back(cur);
  return w;
}

uint32_t FloatBits(float f) {
  uint32_t b;
  std::memcpy(&b, &f, 4);
  return b;
}

struct Spv {
  std::vector<uint32_t> body;
  uint32_t next_id = 1;
  uint32_t id() { return next_id++; }
  void inst(uint32_t op, std::vector<uint32_t> ops) {
    body.push_back((static_cast<uint32_t>(ops.size()) + 1) << 16 | op);
    for (uint32_t w : ops) body.push_back(w);
  }
  void str(uint32_t op, std::vector<uint32_t> ops, const char* s) {
    auto w = StrWords(s);
    ops.insert(ops.end(), w.begin(), w.end());
    inst(op, ops);
  }
  void lit_float(uint32_t bits) { body.push_back(bits); }
  void const_f32(uint32_t type, uint32_t id, float f) {
    inst(kOpConstant, {type, id, FloatBits(f)});
  }
};

}  // namespace

int main() {
  Spv spv;
  // Types
  uint32_t t_void = spv.id(); spv.inst(kOpTypeVoid, {t_void});
  uint32_t t_bool = spv.id(); spv.inst(kOpTypeBool, {t_bool});
  uint32_t t_int = spv.id(); spv.inst(kOpTypeInt, {t_int, 32, 1});
  uint32_t t_uint = spv.id(); spv.inst(kOpTypeInt, {t_uint, 32, 0});
  uint32_t t_f32 = spv.id(); spv.inst(kOpTypeFloat, {t_f32, 32});
  uint32_t t_v2 = spv.id(); spv.inst(kOpTypeVector, {t_v2, t_f32, 2});
  uint32_t t_v4 = spv.id(); spv.inst(kOpTypeVector, {t_v4, t_f32, 4});
  uint32_t t_struct_u = spv.id();
  spv.inst(kOpTypeStruct, {t_struct_u, t_f32, t_f32});
  spv.inst(kOpDecorate, {t_struct_u, kDecBlock});
  spv.str(kOpMemberName, {t_struct_u, 0}, "scale");
  spv.str(kOpMemberName, {t_struct_u, 1}, "flag");
  uint32_t p_uniform = spv.id();
  spv.inst(kOpTypePointer, {p_uniform, kSCUniform, t_struct_u});
  uint32_t p_in_v2 = spv.id(); spv.inst(kOpTypePointer, {p_in_v2, kSCInput, t_v2});
  uint32_t p_in_bool = spv.id(); spv.inst(kOpTypePointer, {p_in_bool, kSCInput, t_bool});
  uint32_t p_in_v4 = spv.id(); spv.inst(kOpTypePointer, {p_in_v4, kSCInput, t_v4});
  uint32_t p_out_v4 = spv.id(); spv.inst(kOpTypePointer, {p_out_v4, kSCOutput, t_v4});
  uint32_t p_f_f32 = spv.id(); spv.inst(kOpTypePointer, {p_f_f32, kSCFunction, t_f32});
  uint32_t p_f_int = spv.id(); spv.inst(kOpTypePointer, {p_f_int, kSCFunction, t_int});
  uint32_t p_f_v4 = spv.id(); spv.inst(kOpTypePointer, {p_f_v4, kSCFunction, t_v4});
  uint32_t t_img = spv.id(); spv.inst(kOpTypeImage, {t_img, t_f32, 1, 0, 0, 0, 1, 0});
  uint32_t t_samp = spv.id(); spv.inst(kOpTypeSampler, {t_samp});
  uint32_t t_simg = spv.id(); spv.inst(kOpTypeSampledImage, {t_simg, t_img});
  uint32_t p_uc_img = spv.id(); spv.inst(kOpTypePointer, {p_uc_img, kSCUniformConstant, t_img});
  uint32_t p_uc_samp = spv.id(); spv.inst(kOpTypePointer, {p_uc_samp, kSCUniformConstant, t_samp});
  uint32_t t_fn = spv.id();
  spv.inst(kOpTypeFunction, {t_fn, t_v4, p_in_v2, p_in_bool, p_in_v4});

  // Constants
  uint32_t c_f0 = spv.id(); spv.const_f32(t_f32, c_f0, 0.f);
  uint32_t c_f025 = spv.id(); spv.const_f32(t_f32, c_f025, 0.25f);
  uint32_t c_f05 = spv.id(); spv.const_f32(t_f32, c_f05, 0.5f);
  uint32_t c_f1 = spv.id(); spv.const_f32(t_f32, c_f1, 1.f);
  uint32_t c_f2 = spv.id(); spv.const_f32(t_f32, c_f2, 2.f);
  uint32_t c_i0 = spv.id(); spv.inst(kOpConstant, {t_int, c_i0, 0});
  uint32_t c_i1 = spv.id(); spv.inst(kOpConstant, {t_int, c_i1, 1});
  uint32_t c_i4 = spv.id(); spv.inst(kOpConstant, {t_int, c_i4, 4});
  uint32_t c_u1 = spv.id(); spv.inst(kOpConstant, {t_uint, c_u1, 1});
  uint32_t c_u3 = spv.id(); spv.inst(kOpConstant, {t_uint, c_u3, 3});
  uint32_t c_v4_1 = spv.id();
  spv.inst(kOpConstantComposite, {t_v4, c_v4_1, c_f1, c_f1, c_f1, c_f1});
  uint32_t ext_glsl = spv.id(); spv.str(kOpExtInstImport, {ext_glsl}, "GLSL.std.450");

  // Globals
  uint32_t in_uv = spv.id(); spv.inst(kOpVariable, {p_in_v2, kSCInput, in_uv});
  spv.inst(kOpDecorate, {in_uv, kDecLocation, 0});
  spv.str(kOpName, {in_uv}, "in_uv");
  uint32_t in_ff = spv.id(); spv.inst(kOpVariable, {p_in_bool, kSCInput, in_ff});
  spv.inst(kOpDecorate, {in_ff, kDecBuiltIn, kBiFrontFacing});
  spv.str(kOpName, {in_ff}, "front_facing");
  uint32_t in_fc = spv.id(); spv.inst(kOpVariable, {p_in_v4, kSCInput, in_fc});
  spv.inst(kOpDecorate, {in_fc, kDecBuiltIn, kBiFragCoord});
  spv.str(kOpName, {in_fc}, "frag_coord");
  uint32_t out_color = spv.id(); spv.inst(kOpVariable, {p_out_v4, kSCOutput, out_color});
  spv.inst(kOpDecorate, {out_color, kDecLocation, 0});
  spv.str(kOpName, {out_color}, "out_color");
  uint32_t u_block = spv.id(); spv.inst(kOpVariable, {p_uniform, kSCUniform, u_block});
  spv.inst(kOpDecorate, {u_block, kDecDescriptorSet, 0});
  spv.inst(kOpDecorate, {u_block, kDecBinding, 0});
  spv.str(kOpName, {u_block}, "uniforms");
  uint32_t tex = spv.id(); spv.inst(kOpVariable, {p_uc_img, kSCUniformConstant, tex});
  spv.inst(kOpDecorate, {tex, kDecDescriptorSet, 0});
  spv.inst(kOpDecorate, {tex, kDecBinding, 1});
  spv.str(kOpName, {tex}, "tex_color");
  uint32_t samp = spv.id(); spv.inst(kOpVariable, {p_uc_samp, kSCUniformConstant, samp});
  spv.inst(kOpDecorate, {samp, kDecDescriptorSet, 0});
  spv.inst(kOpDecorate, {samp, kDecBinding, 2});
  spv.str(kOpName, {samp}, "samp_linear");

  // Entry + function
  uint32_t fn_main = spv.id();
  spv.str(kOpEntryPoint, {1 /*Fragment*/, fn_main, in_uv, in_ff, in_fc}, "main");
  spv.inst(kOpExecutionMode, {fn_main, 0});
  spv.inst(kOpFunction, {t_v4, fn_main, 0, t_fn});
  uint32_t param_uv = spv.id(); spv.inst(kOpFunctionParameter, {p_in_v2, param_uv});
  spv.str(kOpName, {param_uv}, "p_uv");
  uint32_t param_ff = spv.id(); spv.inst(kOpFunctionParameter, {p_in_bool, param_ff});
  spv.str(kOpName, {param_ff}, "p_ff");
  uint32_t param_fc = spv.id(); spv.inst(kOpFunctionParameter, {p_in_v4, param_fc});
  spv.str(kOpName, {param_fc}, "p_fc");

  uint32_t b_entry = spv.id(), b_loop = spv.id(), b_body = spv.id(),
           b_cont = spv.id(), b_exit = spv.id(), b_then = spv.id(),
           b_else = spv.id(), b_merge = spv.id();
  uint32_t acc = spv.id(); spv.inst(kOpVariable, {p_f_f32, kSCFunction, acc});
  spv.str(kOpName, {acc}, "acc");
  uint32_t ivar = spv.id(); spv.inst(kOpVariable, {p_f_int, kSCFunction, ivar});
  spv.str(kOpName, {ivar}, "loop_i");
  uint32_t cvar = spv.id(); spv.inst(kOpVariable, {p_f_v4, kSCFunction, cvar});
  spv.str(kOpName, {cvar}, "color");

  // entry block
  spv.inst(kOpLabel, {b_entry});
  spv.inst(kOpStore, {acc, c_f0});
  spv.inst(kOpStore, {ivar, c_i0});
  uint32_t ld_tex = spv.id(); spv.inst(kOpLoad, {t_img, ld_tex, tex});
  uint32_t ld_samp = spv.id(); spv.inst(kOpLoad, {t_samp, ld_samp, samp});
  uint32_t simg = spv.id(); spv.inst(kOpSampledImage, {t_simg, simg, ld_tex, ld_samp});
  uint32_t ld_uv = spv.id(); spv.inst(kOpLoad, {t_v2, ld_uv, param_uv});
  uint32_t col0 = spv.id(); spv.inst(kOpImageSampleImplicitLod, {t_v4, col0, simg, ld_uv});
  spv.inst(kOpStore, {cvar, col0});
  spv.inst(kOpBranch, {b_loop});

  // loop
  spv.inst(kOpLabel, {b_loop});
  spv.inst(kOpLoopMerge, {b_exit, b_cont, 0, 0});
  spv.inst(kOpBranch, {b_body});
  spv.inst(kOpLabel, {b_body});
  uint32_t ld_i = spv.id(); spv.inst(kOpLoad, {t_int, ld_i, ivar});
  uint32_t cmp_lt = spv.id(); spv.inst(kOpSLessThan, {t_bool, cmp_lt, ld_i, c_i4});
  spv.inst(kOpBranchConditional, {cmp_lt, b_cont, b_exit});
  // continue target
  spv.inst(kOpLabel, {b_cont});
  uint32_t ld_acc = spv.id(); spv.inst(kOpLoad, {t_f32, ld_acc, acc});
  uint32_t acc2 = spv.id(); spv.inst(kOpFAdd, {t_f32, acc2, ld_acc, c_f025});
  spv.inst(kOpStore, {acc, acc2});
  uint32_t i_inc = spv.id(); spv.inst(kOpIAdd, {t_int, i_inc, ld_i, c_i1});
  spv.inst(kOpStore, {ivar, i_inc});
  spv.inst(kOpBranch, {b_loop});

  // after loop
  spv.inst(kOpLabel, {b_exit});
  uint32_t phi_i = spv.id();
  spv.inst(kOpPhi, {t_int, phi_i, b_cont, i_inc, b_exit, c_i0});
  uint32_t ap_flag = spv.id(); spv.inst(kOpAccessChain, {p_f_f32, ap_flag, u_block, c_i1});
  uint32_t ld_flag = spv.id(); spv.inst(kOpLoad, {t_f32, ld_flag, ap_flag});
  uint32_t cmp_flag = spv.id(); spv.inst(kOpFOrdGreaterThan, {t_bool, cmp_flag, ld_flag, c_f05});
  spv.inst(kOpSelectionMerge, {b_merge, 0});
  spv.inst(kOpBranchConditional, {cmp_flag, b_then, b_else});
  spv.inst(kOpLabel, {b_then});
  uint32_t ldp_c = spv.id(); spv.inst(kOpLoad, {t_v4, ldp_c, cvar});
  uint32_t ldp_i = spv.id(); spv.inst(kOpLoad, {t_f32, ldp_i, acc});
  uint32_t m_then = spv.id(); spv.inst(kOpVectorTimesScalar, {t_v4, m_then, ldp_c, ldp_i});
  spv.inst(kOpStore, {cvar, m_then});
  spv.inst(kOpBranch, {b_merge});
  spv.inst(kOpLabel, {b_else});
  uint32_t ld_c2 = spv.id(); spv.inst(kOpLoad, {t_v4, ld_c2, cvar});
  uint32_t m_else = spv.id(); spv.inst(kOpFMul, {t_v4, m_else, ld_c2, c_v4_1});
  spv.inst(kOpStore, {cvar, m_else});
  spv.inst(kOpBranch, {b_merge});
  spv.inst(kOpLabel, {b_merge});
  uint32_t bf = spv.id(); spv.inst(kOpBitFieldUExtract, {t_uint, bf, phi_i, c_u1, c_u3});
  (void)bf;
  uint32_t ld_acc3 = spv.id(); spv.inst(kOpLoad, {t_f32, ld_acc3, acc});
  uint32_t sq = spv.id(); spv.inst(kOpExtInst, {t_f32, sq, ext_glsl, 31, ld_acc3});
  (void)sq;
  uint32_t ld_fc = spv.id(); spv.inst(kOpLoad, {t_v4, ld_fc, param_fc});
  uint32_t dfx = spv.id(); spv.inst(kOpDPdx, {t_v4, dfx, ld_fc});
  (void)dfx;
  uint32_t ld_c3 = spv.id(); spv.inst(kOpLoad, {t_v4, ld_c3, cvar});
  uint32_t r_inv = spv.id(); spv.inst(kOpFDiv, {t_f32, r_inv, c_f1, sq});
  uint32_t scaled = spv.id(); spv.inst(kOpVectorTimesScalar, {t_v4, scaled, ld_c3, r_inv});
  spv.inst(kOpStore, {out_color, scaled});
  spv.inst(kOpReturnValue, {scaled});
  spv.inst(kOpFunctionEnd, {});

  std::vector<uint32_t> module;
  module.push_back(0x07230203u);
  module.push_back(0x00010000u);
  module.push_back(0x00080001u);
  module.push_back(spv.next_id);
  module.push_back(0);
  for (uint32_t w : spv.body) module.push_back(w);

  WgslTranslationResult result;
  bool ok = TranslateSpirvToWgsl(module.data(), module.size(), 1, result);
  if (!ok) {
    printf("TRANSLATION FAILED: %s\n", result.error.c_str());
    return 1;
  }
  printf("=== WGSL OUTPUT ===\n%s=== END ===\n", result.wgsl.c_str());

  int braces = 0;
  for (char c : result.wgsl) {
    if (c == '{') braces++;
    if (c == '}') braces--;
  }
  bool has_main = result.wgsl.find("fn main(") != std::string::npos;
  bool has_sample = result.wgsl.find("textureSample(") != std::string::npos;
  bool has_ret = result.wgsl.find("return") != std::string::npos;
  bool has_loop = result.wgsl.find("loop") != std::string::npos;
  bool has_if = result.wgsl.find("if ") != std::string::npos;
  bool no_hash = result.wgsl.find("#id") == std::string::npos;
  printf("braces:%s main:%s sample:%s ret:%s loop:%s if:%s unresolved-ids:%s\n",
         braces == 0 ? "OK" : "BAD", has_main ? "Y" : "N",
         has_sample ? "Y" : "N", has_ret ? "Y" : "N", has_loop ? "Y" : "N",
         has_if ? "Y" : "N", no_hash ? "none" : "FOUND");
  return (braces == 0 && has_main && has_sample && has_ret && has_loop &&
          has_if && no_hash)
             ? 0
             : 2;
}
