
#ifndef __SYNONYMOUS_GLYPHS
#define __SYNONYMOUS_GLYPHS

#include <unordered_map>

typedef unsigned int Unicode;

// ToDo: Compute this heuristic using the Unicode confusion matrix computed by cross validation
// As a simple heuristic, map semantically and visually similar glyphs to the lower of the
// unicode values
const static std::unordered_map<Unicode, Unicode> equivalent_glyphs = 
{
    {0x39c, 0x4d}, // Greek Capital Letter Mu -> Latin Capital letter M
    {0x3bc, 0xb5}, // Greek Small Letter Mu -> Micro sign
    {0xa0, 0x20},  // Non-breaking space -> Space
    {0x2003, 0x20}, // Space -> Space
    {0x2009, 0x20}, // Thin space -> Space
    {0x2010, 0x2d}, // Hyphen -> Dash/minus
    {0x2011, 0x2d}, // Non-breaking hyphe -> Dash/minus
    {0x2013, 0x2d}, // En dash -> Dash/minus
    {0x2014, 0x2d}, // Em dash -> Dash/minus
    {0x2212, 0x2d}, // Minus sign -> Dash/minus
    {0x204e, 0x2a}, // Asterix -> Asterix
    {0x2206, 0x394}, // Delta -> Greek Capital Letter Delta
    {0x2219, 0xb7}, // Bullet -> Middle dot
    {0x424, 0x3a6}, // Cyrillic Capital Letter Ef -> Greek Capital Letter Phi
    {0x3d5, 0x3c6}, // Greek Phi Symbol -> Greek Small Letter Phi
    {0x2126, 0x3a9}, // Ohm sign -> Greek Capital Letter Omega
    {0x37e, 0x3b} // Greek Question Mark -> Semi colon
};

#endif //__SYNONYMOUS_GLYPHS