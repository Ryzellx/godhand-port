// God Hand (PS2 SLUS-21503) — CRI ADX ADPCM decoder (port audio backend)
// Runtime: C++17 host (no PS2 deps) | Target: native port audio path
//
// Two arithmetic flavours are implemented because the two independent
// reference decoders disagree on details that matter for byte-exactness:
//   AdxFlavor::Cri    - CRI ADXPLAY / vgmstream behaviour (scale+1, header
//                       history seed, truncated float coefficients).
//   AdxFlavor::Ffmpeg - libavcodec adpcm_adx behaviour (raw scale, zero
//                       history, lrintf-rounded coefficients).
// Both share the same nibble/predictor core, so a byte-exact match in either
// flavour proves the ADPCM math; the delta between flavours is the seed and
// one coefficient LSB, and is reported by src/shim/adx_test.cpp.
//
// Encryption: CRI type 8 (version word 0x0408) scrambles ONLY the 2-byte
// scale word of each frame with an LCG keystream seeded from a keystring.
// God Hand's keystring is "karaage" (ASCII, present in SLUS_215.03), derived
// here through the same 0x400-entry prime table CRI_ADXI.IRX uses, which
// yields start=0x49E1 mult=0x4A57 add=0x553D.
#include "adx.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace gh {
namespace audio {

const char* const kAdxGodHandKeystring = "karaage";

namespace {

// Prime table used by CRI's ADX_Decoder keystring expansion (0x400 entries,
// every prime in 0x401B..0x6839 range padded to 16-bit). Copied verbatim from
// the CRI decoder model so keystrings can be re-derived instead of hardcoded.
const uint16_t kKey8Primes[0x400] = {
    0x401B, 0x4021, 0x4025, 0x402B, 0x4031, 0x403F, 0x4043, 0x4045, 0x405D, 0x4061, 0x4067, 0x406D,
    0x4087, 0x4091, 0x40A3, 0x40A9, 0x40B1, 0x40B7, 0x40BD, 0x40DB, 0x40DF, 0x40EB, 0x40F7, 0x40F9,
    0x4109, 0x410B, 0x4111, 0x4115, 0x4121, 0x4133, 0x4135, 0x413B, 0x413F, 0x4159, 0x4165, 0x416B,
    0x4177, 0x417B, 0x4193, 0x41AB, 0x41B7, 0x41BD, 0x41BF, 0x41CB, 0x41E7, 0x41EF, 0x41F3, 0x41F9,
    0x4205, 0x4207, 0x4219, 0x421F, 0x4223, 0x4229, 0x422F, 0x4243, 0x4253, 0x4255, 0x425B, 0x4261,
    0x4273, 0x427D, 0x4283, 0x4285, 0x4289, 0x4291, 0x4297, 0x429D, 0x42B5, 0x42C5, 0x42CB, 0x42D3,
    0x42DD, 0x42E3, 0x42F1, 0x4307, 0x430F, 0x431F, 0x4325, 0x4327, 0x4333, 0x4337, 0x4339, 0x434F,
    0x4357, 0x4369, 0x438B, 0x438D, 0x4393, 0x43A5, 0x43A9, 0x43AF, 0x43B5, 0x43BD, 0x43C7, 0x43CF,
    0x43E1, 0x43E7, 0x43EB, 0x43ED, 0x43F1, 0x43F9, 0x4409, 0x440B, 0x4417, 0x4423, 0x4429, 0x443B,
    0x443F, 0x4445, 0x444B, 0x4451, 0x4453, 0x4459, 0x4465, 0x446F, 0x4483, 0x448F, 0x44A1, 0x44A5,
    0x44AB, 0x44AD, 0x44BD, 0x44BF, 0x44C9, 0x44D7, 0x44DB, 0x44F9, 0x44FB, 0x4505, 0x4511, 0x4513,
    0x452B, 0x4531, 0x4541, 0x4549, 0x4553, 0x4555, 0x4561, 0x4577, 0x457D, 0x457F, 0x458F, 0x45A3,
    0x45AD, 0x45AF, 0x45BB, 0x45C7, 0x45D9, 0x45E3, 0x45EF, 0x45F5, 0x45F7, 0x4601, 0x4603, 0x4609,
    0x4613, 0x4625, 0x4627, 0x4633, 0x4639, 0x463D, 0x4643, 0x4645, 0x465D, 0x4679, 0x467B, 0x467F,
    0x4681, 0x468B, 0x468D, 0x469D, 0x46A9, 0x46B1, 0x46C7, 0x46C9, 0x46CF, 0x46D3, 0x46D5, 0x46DF,
    0x46E5, 0x46F9, 0x4705, 0x470F, 0x4717, 0x4723, 0x4729, 0x472F, 0x4735, 0x4739, 0x474B, 0x474D,
    0x4751, 0x475D, 0x476F, 0x4771, 0x477D, 0x4783, 0x4787, 0x4789, 0x4799, 0x47A5, 0x47B1, 0x47BF,
    0x47C3, 0x47CB, 0x47DD, 0x47E1, 0x47ED, 0x47FB, 0x4801, 0x4807, 0x480B, 0x4813, 0x4819, 0x481D,
    0x4831, 0x483D, 0x4847, 0x4855, 0x4859, 0x485B, 0x486B, 0x486D, 0x4879, 0x4897, 0x489B, 0x48A1,
    0x48B9, 0x48CD, 0x48E5, 0x48EF, 0x48F7, 0x4903, 0x490D, 0x4919, 0x491F, 0x492B, 0x4937, 0x493D,
    0x4945, 0x4955, 0x4963, 0x4969, 0x496D, 0x4973, 0x4997, 0x49AB, 0x49B5, 0x49D3, 0x49DF, 0x49E1,
    0x49E5, 0x49E7, 0x4A03, 0x4A0F, 0x4A1D, 0x4A23, 0x4A39, 0x4A41, 0x4A45, 0x4A57, 0x4A5D, 0x4A6B,
    0x4A7D, 0x4A81, 0x4A87, 0x4A89, 0x4A8F, 0x4AB1, 0x4AC3, 0x4AC5, 0x4AD5, 0x4ADB, 0x4AED, 0x4AEF,
    0x4B07, 0x4B0B, 0x4B0D, 0x4B13, 0x4B1F, 0x4B25, 0x4B31, 0x4B3B, 0x4B43, 0x4B49, 0x4B59, 0x4B65,
    0x4B6D, 0x4B77, 0x4B85, 0x4BAD, 0x4BB3, 0x4BB5, 0x4BBB, 0x4BBF, 0x4BCB, 0x4BD9, 0x4BDD, 0x4BDF,
    0x4BE3, 0x4BE5, 0x4BE9, 0x4BF1, 0x4BF7, 0x4C01, 0x4C07, 0x4C0D, 0x4C0F, 0x4C15, 0x4C1B, 0x4C21,
    0x4C2D, 0x4C33, 0x4C4B, 0x4C55, 0x4C57, 0x4C61, 0x4C67, 0x4C73, 0x4C79, 0x4C7F, 0x4C8D, 0x4C93,
    0x4C99, 0x4CCD, 0x4CE1, 0x4CE7, 0x4CF1, 0x4CF3, 0x4CFD, 0x4D05, 0x4D0F, 0x4D1B, 0x4D27, 0x4D29,
    0x4D2F, 0x4D33, 0x4D41, 0x4D51, 0x4D59, 0x4D65, 0x4D6B, 0x4D81, 0x4D83, 0x4D8D, 0x4D95, 0x4D9B,
    0x4DB1, 0x4DB3, 0x4DC9, 0x4DCF, 0x4DD7, 0x4DE1, 0x4DED, 0x4DF9, 0x4DFB, 0x4E05, 0x4E0B, 0x4E17,
    0x4E19, 0x4E1D, 0x4E2B, 0x4E35, 0x4E37, 0x4E3D, 0x4E4F, 0x4E53, 0x4E5F, 0x4E67, 0x4E79, 0x4E85,
    0x4E8B, 0x4E91, 0x4E95, 0x4E9B, 0x4EA1, 0x4EAF, 0x4EB3, 0x4EB5, 0x4EC1, 0x4ECD, 0x4ED1, 0x4ED7,
    0x4EE9, 0x4EFB, 0x4F07, 0x4F09, 0x4F19, 0x4F25, 0x4F2D, 0x4F3F, 0x4F49, 0x4F63, 0x4F67, 0x4F6D,
    0x4F75, 0x4F7B, 0x4F81, 0x4F85, 0x4F87, 0x4F91, 0x4FA5, 0x4FA9, 0x4FAF, 0x4FB7, 0x4FBB, 0x4FCF,
    0x4FD9, 0x4FDB, 0x4FFD, 0x4FFF, 0x5003, 0x501B, 0x501D, 0x5029, 0x5035, 0x503F, 0x5045, 0x5047,
    0x5053, 0x5071, 0x5077, 0x5083, 0x5093, 0x509F, 0x50A1, 0x50B7, 0x50C9, 0x50D5, 0x50E3, 0x50ED,
    0x50EF, 0x50FB, 0x5107, 0x510B, 0x510D, 0x5111, 0x5117, 0x5123, 0x5125, 0x5135, 0x5147, 0x5149,
    0x5171, 0x5179, 0x5189, 0x518F, 0x5197, 0x51A1, 0x51A3, 0x51A7, 0x51B9, 0x51C1, 0x51CB, 0x51D3,
    0x51DF, 0x51E3, 0x51F5, 0x51F7, 0x5209, 0x5213, 0x5215, 0x5219, 0x521B, 0x521F, 0x5227, 0x5243,
    0x5245, 0x524B, 0x5261, 0x526D, 0x5273, 0x5281, 0x5293, 0x5297, 0x529D, 0x52A5, 0x52AB, 0x52B1,
    0x52BB, 0x52C3, 0x52C7, 0x52C9, 0x52DB, 0x52E5, 0x52EB, 0x52FF, 0x5315, 0x531D, 0x5323, 0x5341,
    0x5345, 0x5347, 0x534B, 0x535D, 0x5363, 0x5381, 0x5383, 0x5387, 0x538F, 0x5395, 0x5399, 0x539F,
    0x53AB, 0x53B9, 0x53DB, 0x53E9, 0x53EF, 0x53F3, 0x53F5, 0x53FB, 0x53FF, 0x540D, 0x5411, 0x5413,
    0x5419, 0x5435, 0x5437, 0x543B, 0x5441, 0x5449, 0x5453, 0x5455, 0x545F, 0x5461, 0x546B, 0x546D,
    0x5471, 0x548F, 0x5491, 0x549D, 0x54A9, 0x54B3, 0x54C5, 0x54D1, 0x54DF, 0x54E9, 0x54EB, 0x54F7,
    0x54FD, 0x5507, 0x550D, 0x551B, 0x5527, 0x552B, 0x5539, 0x553D, 0x554F, 0x5551, 0x555B, 0x5563,
    0x5567, 0x556F, 0x5579, 0x5585, 0x5597, 0x55A9, 0x55B1, 0x55B7, 0x55C9, 0x55D9, 0x55E7, 0x55ED,
    0x55F3, 0x55FD, 0x560B, 0x560F, 0x5615, 0x5617, 0x5623, 0x562F, 0x5633, 0x5639, 0x563F, 0x564B,
    0x564D, 0x565D, 0x565F, 0x566B, 0x5671, 0x5675, 0x5683, 0x5689, 0x568D, 0x568F, 0x569B, 0x56AD,
    0x56B1, 0x56D5, 0x56E7, 0x56F3, 0x56FF, 0x5701, 0x5705, 0x5707, 0x570B, 0x5713, 0x571F, 0x5723,
    0x5747, 0x574D, 0x575F, 0x5761, 0x576D, 0x5777, 0x577D, 0x5789, 0x57A1, 0x57A9, 0x57AF, 0x57B5,
    0x57C5, 0x57D1, 0x57D3, 0x57E5, 0x57EF, 0x5803, 0x580D, 0x580F, 0x5815, 0x5827, 0x582B, 0x582D,
    0x5855, 0x585B, 0x585D, 0x586D, 0x586F, 0x5873, 0x587B, 0x588D, 0x5897, 0x58A3, 0x58A9, 0x58AB,
    0x58B5, 0x58BD, 0x58C1, 0x58C7, 0x58D3, 0x58D5, 0x58DF, 0x58F1, 0x58F9, 0x58FF, 0x5903, 0x5917,
    0x591B, 0x5921, 0x5945, 0x594B, 0x594D, 0x5957, 0x595D, 0x5975, 0x597B, 0x5989, 0x5999, 0x599F,
    0x59B1, 0x59B3, 0x59BD, 0x59D1, 0x59DB, 0x59E3, 0x59E9, 0x59ED, 0x59F3, 0x59F5, 0x59FF, 0x5A01,
    0x5A0D, 0x5A11, 0x5A13, 0x5A17, 0x5A1F, 0x5A29, 0x5A2F, 0x5A3B, 0x5A4D, 0x5A5B, 0x5A67, 0x5A77,
    0x5A7F, 0x5A85, 0x5A95, 0x5A9D, 0x5AA1, 0x5AA3, 0x5AA9, 0x5ABB, 0x5AD3, 0x5AE5, 0x5AEF, 0x5AFB,
    0x5AFD, 0x5B01, 0x5B0F, 0x5B19, 0x5B1F, 0x5B25, 0x5B2B, 0x5B3D, 0x5B49, 0x5B4B, 0x5B67, 0x5B79,
    0x5B87, 0x5B97, 0x5BA3, 0x5BB1, 0x5BC9, 0x5BD5, 0x5BEB, 0x5BF1, 0x5BF3, 0x5BFD, 0x5C05, 0x5C09,
    0x5C0B, 0x5C0F, 0x5C1D, 0x5C29, 0x5C2F, 0x5C33, 0x5C39, 0x5C47, 0x5C4B, 0x5C4D, 0x5C51, 0x5C6F,
    0x5C75, 0x5C77, 0x5C7D, 0x5C87, 0x5C89, 0x5CA7, 0x5CBD, 0x5CBF, 0x5CC3, 0x5CC9, 0x5CD1, 0x5CD7,
    0x5CDD, 0x5CED, 0x5CF9, 0x5D05, 0x5D0B, 0x5D13, 0x5D17, 0x5D19, 0x5D31, 0x5D3D, 0x5D41, 0x5D47,
    0x5D4F, 0x5D55, 0x5D5B, 0x5D65, 0x5D67, 0x5D6D, 0x5D79, 0x5D95, 0x5DA3, 0x5DA9, 0x5DAD, 0x5DB9,
    0x5DC1, 0x5DC7, 0x5DD3, 0x5DD7, 0x5DDD, 0x5DEB, 0x5DF1, 0x5DFD, 0x5E07, 0x5E0D, 0x5E13, 0x5E1B,
    0x5E21, 0x5E27, 0x5E2B, 0x5E2D, 0x5E31, 0x5E39, 0x5E45, 0x5E49, 0x5E57, 0x5E69, 0x5E73, 0x5E75,
    0x5E85, 0x5E8B, 0x5E9F, 0x5EA5, 0x5EAF, 0x5EB7, 0x5EBB, 0x5ED9, 0x5EFD, 0x5F09, 0x5F11, 0x5F27,
    0x5F33, 0x5F35, 0x5F3B, 0x5F47, 0x5F57, 0x5F5D, 0x5F63, 0x5F65, 0x5F77, 0x5F7B, 0x5F95, 0x5F99,
    0x5FA1, 0x5FB3, 0x5FBD, 0x5FC5, 0x5FCF, 0x5FD5, 0x5FE3, 0x5FE7, 0x5FFB, 0x6011, 0x6023, 0x602F,
    0x6037, 0x6053, 0x605F, 0x6065, 0x606B, 0x6073, 0x6079, 0x6085, 0x609D, 0x60AD, 0x60BB, 0x60BF,
    0x60CD, 0x60D9, 0x60DF, 0x60E9, 0x60F5, 0x6109, 0x610F, 0x6113, 0x611B, 0x612D, 0x6139, 0x614B,
    0x6155, 0x6157, 0x615B, 0x616F, 0x6179, 0x6187, 0x618B, 0x6191, 0x6193, 0x619D, 0x61B5, 0x61C7,
    0x61C9, 0x61CD, 0x61E1, 0x61F1, 0x61FF, 0x6209, 0x6217, 0x621D, 0x6221, 0x6227, 0x623B, 0x6241,
    0x624B, 0x6251, 0x6253, 0x625F, 0x6265, 0x6283, 0x628D, 0x6295, 0x629B, 0x629F, 0x62A5, 0x62AD,
    0x62D5, 0x62D7, 0x62DB, 0x62DD, 0x62E9, 0x62FB, 0x62FF, 0x6305, 0x630D, 0x6317, 0x631D, 0x632F,
    0x6341, 0x6343, 0x634F, 0x635F, 0x6367, 0x636D, 0x6371, 0x6377, 0x637D, 0x637F, 0x63B3, 0x63C1,
    0x63C5, 0x63D9, 0x63E9, 0x63EB, 0x63EF, 0x63F5, 0x6401, 0x6403, 0x6409, 0x6415, 0x6421, 0x6427,
    0x642B, 0x6439, 0x6443, 0x6449, 0x644F, 0x645D, 0x6467, 0x6475, 0x6485, 0x648D, 0x6493, 0x649F,
    0x64A3, 0x64AB, 0x64C1, 0x64C7, 0x64C9, 0x64DB, 0x64F1, 0x64F7, 0x64F9, 0x650B, 0x6511, 0x6521,
    0x652F, 0x6539, 0x653F, 0x654B, 0x654D, 0x6553, 0x6557, 0x655F, 0x6571, 0x657D, 0x658D, 0x658F,
    0x6593, 0x65A1, 0x65A5, 0x65AD, 0x65B9, 0x65C5, 0x65E3, 0x65F3, 0x65FB, 0x65FF, 0x6601, 0x6607,
    0x661D, 0x6629, 0x6631, 0x663B, 0x6641, 0x6647, 0x664D, 0x665B, 0x6661, 0x6673, 0x667D, 0x6689,
    0x668B, 0x6695, 0x6697, 0x669B, 0x66B5, 0x66B9, 0x66C5, 0x66CD, 0x66D1, 0x66E3, 0x66EB, 0x66F5,
    0x6703, 0x6713, 0x6719, 0x671F, 0x6727, 0x6731, 0x6737, 0x673F, 0x6745, 0x6751, 0x675B, 0x676F,
    0x6779, 0x6781, 0x6785, 0x6791, 0x67AB, 0x67BD, 0x67C1, 0x67CD, 0x67DF, 0x67E5, 0x6803, 0x6809,
    0x6811, 0x6817, 0x682D, 0x6839,
};

inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
inline int16_t rds16(const uint8_t* p) { return (int16_t)rd16(p); }
inline uint32_t rd32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
inline int32_t rds32(const uint8_t* p) { return (int32_t)rd32(p); }

inline int32_t clamp16(int32_t v) {
    if (v < -32768) return -32768;
    if (v > 32767) return 32767;
    return v;
}

struct Lpc {
    int32_t c1 = 0;
    int32_t c2 = 0;
};

// Per-frame predictor table for encoding type 2 ("ADX with fixed coefficients",
// the only variant that carries a real filter index): 12-bit Q coefficients
// for filter indices 0..3, standard XA values scaled by (2 << 11).
const int32_t kFixedCoef[4][2] = {
    {0x0000, 0x0000},   // filter 0: no prediction
    {0x0F00, 0x0000},   // filter 1
    {0x1CC0, -3328},    // filter 2 (0xF300 as int16)
    {0x1880, -3520},    // filter 3 (0xF240 as int16)
};

// CRI ADXPLAY: float math, truncating cast into 16-bit coefficients.
Lpc coefs_cri(uint16_t cutoff, uint32_t sample_rate) {
    const float x = (float)cutoff;
    const float y = (float)sample_rate;
    const float z = cosf((float)(2.0 * M_PI * x / y));
    const float a = (float)(1.4142135623730950488 - (double)z);
    const float b = (float)(1.4142135623730950488 - 1.0);
    const float c = (a - sqrtf((a + b) * (a - b))) / b;
    Lpc r;
    r.c1 = (int16_t)(c * 8192.0f);
    r.c2 = (int16_t)(c * c * -4096.0f);
    return r;
}

// libavcodec adpcm_adx: double math, lrintf rounding.
Lpc coefs_ffmpeg(uint16_t cutoff, uint32_t sample_rate) {
    const double a = M_SQRT2 - cos(2.0 * M_PI * cutoff / sample_rate);
    const double b = M_SQRT2 - 1.0;
    const double c = (a - sqrt((a + b) * (a - b))) / b;
    Lpc r;
    r.c1 = (int32_t)lrintf((float)(c * 2.0 * 4096.0));
    r.c2 = (int32_t)lrintf((float)(-(c * c) * 4096.0));
    return r;
}

// Decodes one 18-byte frame into `out` with a stride of `channels` int16s.
// h1/h2 carry the per-channel predictor state across frames.
void decode_frame(const uint8_t* frame, uint8_t encoding, AdxFlavor flavor,
                  const Lpc& header_coef, int32_t* h1, int32_t* h2,
                  int16_t* out, size_t stride) {
    const uint16_t word = rd16(frame);
    int32_t scale;
    Lpc coef = header_coef;

    if (encoding == 2) {
        // Fixed-coefficient variant: top 3 bits select the filter table entry.
        const uint32_t filter = (word >> 13) & 0x03;
        coef.c1 = kFixedCoef[filter][0];
        coef.c2 = kFixedCoef[filter][1];
        scale = (int32_t)(word & 0x1fff) + 1;
    } else if (encoding == 4) {
        // Exponential scale variant (never seen in God Hand, kept for parity).
        const int32_t shift = 12 - (int32_t)word;
        scale = (shift >= 0 && shift <= 15) ? (1 << shift) : 0;
    } else if (flavor == AdxFlavor::Cri) {
        scale = (int32_t)(int16_t)word + 1;
        if (frame[0] == 0x80 && frame[1] == 0x01) scale = 0;  // 0x8001 EOF marker
    } else {
        scale = (word & 0x8000) ? 0 : (int32_t)word;  // ffmpeg: 0x8000 = EOF packet
    }

    int32_t s1 = *h1;
    int32_t s2 = *h2;
    for (uint32_t i = 0; i < kAdxFrameSamples; ++i) {
        const uint8_t byte = frame[2 + (i >> 1)];
        int32_t nib = (i & 1) ? (int32_t)(byte & 0x0f) : (int32_t)(byte >> 4);
        if (nib >= 8) nib -= 16;  // 4-bit two's complement
        // Arithmetic shift of the negative-capable predictor sum, as both
        // reference decoders do on gcc/clang.
        const int32_t s = clamp16(nib * scale + ((coef.c1 * s1 + coef.c2 * s2) >> kAdxCoefBits));
        out[i * stride] = (int16_t)s;
        s2 = s1;
        s1 = s;
    }
    *h1 = s1;
    *h2 = s2;
}

}  // namespace

AdxKey adx_key_from_keystring(const char* keystring) {
    AdxKey key;
    if (!keystring || !keystring[0]) return key;
    uint16_t k1 = kKey8Primes[0x100];
    uint16_t k2 = kKey8Primes[0x200];
    uint16_t k3 = kKey8Primes[0x300];
    for (const char* p = keystring; *p; ++p) {
        const uint32_t c = (uint32_t)(uint8_t)*p;
        if (c + 0x80 >= 0x400) return AdxKey{};  // keystrings are ASCII/sjis
        const uint32_t prime = kKey8Primes[c + 0x80];
        k1 = kKey8Primes[(k1 * prime) % 0x400];
        k2 = kKey8Primes[(k2 * prime) % 0x400];
        k3 = kKey8Primes[(k3 * prime) % 0x400];
    }
    key.start = k1;
    key.mult = k2;
    key.add = k3;
    key.valid = true;
    return key;
}

bool adx_is_adx(const uint8_t* data, size_t size) {
    AdxHeader h;
    return adx_parse_header(data, size, &h, nullptr);
}

bool adx_parse_header(const uint8_t* d, size_t n, AdxHeader* out, std::string* err) {
    auto fail = [&](const char* msg) {
        if (err) *err = msg;
        return false;
    };
    if (!d) return fail("null buffer");
    if (n < 0x28) return fail("buffer shorter than minimum ADX header");
    if (d[0] != 0x80 || d[1] != 0x00) return fail("bad magic (expected 0x8000)");

    AdxHeader h;
    h.header_size = rd16(d + 2);
    h.data_offset = (uint32_t)h.header_size + 4;
    if (h.data_offset < 6 || h.data_offset > n) return fail("header size out of range");
    if (std::memcmp(d + h.data_offset - 6, "(c)CRI", 6) != 0) return fail("missing (c)CRI trailer");

    h.encoding = d[4];
    h.frame_size = d[5];
    h.bits_per_sample = d[6];
    h.channels = d[7];
    if (h.encoding < 2 || h.encoding > 4) return fail("unsupported encoding type");
    if (h.frame_size != kAdxFrameBytes) return fail("unsupported frame size");
    if (h.bits_per_sample != 4) return fail("unsupported bit depth");
    if (h.channels == 0 || h.channels > kAdxMaxChannels) return fail("channel count out of range");

    h.sample_rate = rd32(d + 8);
    h.sample_count = rd32(d + 0x0c);
    h.cutoff_hz = rd16(d + 0x10);
    h.version_word = rd16(d + 0x12);
    h.encrypted = (h.version_word == 0x0408 || h.version_word == 0x0409);
    if (h.sample_rate < 1 || h.sample_rate > 768000) return fail("sample rate out of range");

    const uint16_t ver = (uint16_t)(h.version_word & 0x0f00);
    if (ver != 0x0300 && ver != 0x0400 && ver != 0x0500) return fail("unknown ADX version");

    // v4: history seed at 0x18 (4 bytes per channel), loop block right after it.
    // v3: no history, loop block at 0x14.  Loop block is only present when the
    // header actually has room before the (c)CRI trailer.
    uint32_t hist_off = 0;
    uint32_t hist_size = 0;
    uint32_t loops_off = 0;
    if (ver == 0x0400) {
        hist_off = 0x18;
        hist_size = (h.channels > 1) ? (4u * h.channels) : 8u;
        loops_off = 0x18 + hist_size;
    } else if (ver == 0x0300) {
        loops_off = 0x14;
    } else {
        loops_off = 0x14;  // v5 (SFD): treated as absent, room check below rejects
    }

    const uint32_t room = h.data_offset - 6;
    if (loops_off && loops_off + 0x18 <= room) {
        h.loop_padding = rd16(d + loops_off);
        h.loop = rds32(d + loops_off + 0x04) != 0;
        h.loop_start_sample = rds32(d + loops_off + 0x08);
        h.loop_start_byte = rd32(d + loops_off + 0x0c);
        h.loop_end_sample = rds32(d + loops_off + 0x10);
        h.loop_end_byte = rd32(d + loops_off + 0x14);
        if (h.loop_start_sample < 0) h.loop_start_sample = 0;
        if (h.loop_end_sample < h.loop_start_sample) h.loop_end_sample = h.loop_start_sample;
    }
    if (hist_off && hist_off + 4u * h.channels <= room) {
        for (uint32_t c = 0; c < h.channels; ++c) {
            h.history[c][0] = rds16(d + hist_off + c * 4 + 0);
            h.history[c][1] = rds16(d + hist_off + c * 4 + 2);
        }
    }

    *out = h;
    return true;
}

bool adx_decrypt_to_plain(const uint8_t* data, size_t size, const AdxKey& key,
                          const AdxHeader& header, std::vector<uint8_t>* out) {
    if (!data || !out || !key.valid) return false;
    if (header.data_offset >= size) return false;
    std::vector<uint8_t> buf(data, data + size);

    // One keystream step per 18-byte frame, across channels in stream order.
    const size_t frames = (size - header.data_offset) / header.frame_size;
    uint16_t state = key.start;
    for (size_t f = 0; f < frames; ++f) {
        uint8_t* word = buf.data() + header.data_offset + f * (size_t)header.frame_size;
        const uint16_t plain = (uint16_t)(rd16(word) ^ state);
        word[0] = (uint8_t)((plain >> 8) & 0x1f);  // keep 13-bit scale, clear filter bits
        word[1] = (uint8_t)(plain & 0xff);
        state = (uint16_t)((state * key.mult + key.add) & 0x7fff);
    }
    // Mark the copy as plain v4 so external decoders do not expect a cipher.
    buf[0x12] = 0x04;
    buf[0x13] = 0x00;
    *out = std::move(buf);
    return true;
}

bool AdxDecoder::init(const uint8_t* data, size_t size, AdxFlavor flavor,
                      const AdxKey& key, std::string* err) {
    data_.clear();
    pcm_.clear();
    pcm_trimmed_.clear();
    decoded_ = false;
    trimmed_ = false;
    samples_per_channel_ = 0;
    samples_per_channel_trimmed_ = 0;
    flavor_ = flavor;

    AdxHeader h;
    if (!adx_parse_header(data, size, &h, err)) return false;
    data_.assign(data, data + size);

    if (h.encrypted) {
        AdxKey k = key.valid ? key : adx_key_from_keystring(kAdxGodHandKeystring);
        if (!k.valid) {
            if (err) *err = "encrypted stream but no usable ADX key";
            return false;
        }
        std::vector<uint8_t> plain;
        if (!adx_decrypt_to_plain(data_.data(), data_.size(), k, h, &plain)) {
            if (err) *err = "decryption failed";
            return false;
        }
        data_ = std::move(plain);
        // Header bytes are unchanged except the version word.
        if (!adx_parse_header(data_.data(), data_.size(), &h, err)) return false;
    }
    header_ = h;
    return true;
}

const std::vector<int16_t>& AdxDecoder::decode() {
    if (decoded_) return pcm_;
    decoded_ = true;

    const size_t channels = header_.channels;
    const size_t frame_bytes = (size_t)header_.frame_size * channels;
    if (header_.data_offset >= data_.size()) return pcm_;
    const size_t blocks = (data_.size() - header_.data_offset) / frame_bytes;
    samples_per_channel_ = blocks * kAdxFrameSamples;
    pcm_.assign(samples_per_channel_ * channels, 0);

    const Lpc header_coef = (flavor_ == AdxFlavor::Cri)
                                ? coefs_cri(header_.cutoff_hz, header_.sample_rate)
                                : coefs_ffmpeg(header_.cutoff_hz, header_.sample_rate);

    int32_t h1[kAdxMaxChannels] = {0};
    int32_t h2[kAdxMaxChannels] = {0};
    if (flavor_ == AdxFlavor::Cri) {
        for (size_t c = 0; c < channels; ++c) {
            h1[c] = header_.history[c][0];
            h2[c] = header_.history[c][1];
        }
    }

    const uint8_t* body = data_.data() + header_.data_offset;
    for (size_t b = 0; b < blocks; ++b) {
        int16_t* block_out = pcm_.data() + b * kAdxFrameSamples * channels;
        for (size_t c = 0; c < channels; ++c) {
            decode_frame(body + b * frame_bytes + c * header_.frame_size, header_.encoding,
                         flavor_, header_coef, &h1[c], &h2[c], block_out + c, channels);
        }
    }
    return pcm_;
}

size_t AdxDecoder::samples_per_channel_trimmed() const {
    const size_t want = header_.sample_count;
    if (want == 0) return samples_per_channel_;
    return want < samples_per_channel_ ? want : samples_per_channel_;
}

const std::vector<int16_t>& AdxDecoder::decode_trimmed() {
    if (trimmed_) return pcm_trimmed_;
    decode();
    trimmed_ = true;
    samples_per_channel_trimmed_ = samples_per_channel_trimmed();
    pcm_trimmed_.assign(pcm_.begin(),
                        pcm_.begin() + (ptrdiff_t)(samples_per_channel_trimmed_ * header_.channels));
    return pcm_trimmed_;
}

bool adx_decode_file(const char* path, AdxFlavor flavor, const AdxKey& key,
                     AdxHeader* header, std::vector<int16_t>* pcm, std::string* err) {
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        if (err) *err = std::string("cannot open ") + path;
        return false;
    }
    std::vector<uint8_t> buf;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        std::fclose(f);
        if (err) *err = "empty file";
        return false;
    }
    buf.resize((size_t)n);
    const size_t got = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    if (got != buf.size()) {
        if (err) *err = "short read";
        return false;
    }

    AdxDecoder dec;
    if (!dec.init(buf.data(), buf.size(), flavor, key, err)) return false;
    if (header) *header = dec.header();
    if (pcm) *pcm = dec.decode();
    return true;
}

}  // namespace audio
}  // namespace gh
