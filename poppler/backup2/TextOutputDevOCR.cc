//========================================================================
//
// TextOutputDevOCR.cc
//
// Copyright 1997-2003 Glyph & Cog, LLC
//
//========================================================================

//========================================================================
//
// Modified under the Poppler project - http://poppler.freedesktop.org
//
// All changes made under the Poppler project to this file are licensed
// under GPL version 2 or later
//
// Copyright (C) 2005-2007 Kristian Høgsberg <krh@redhat.com>
// Copyright (C) 2005 Nickolay V. Shmyrev <nshmyrev@yandex.ru>
// Copyright (C) 2006-2008, 2011-2013 Carlos Garcia Campos <carlosgc@gnome.org>
// Copyright (C) 2006, 2007, 2013 Ed Catmur <ed@catmur.co.uk>
// Copyright (C) 2006 Jeff Muizelaar <jeff@infidigm.net>
// Copyright (C) 2007, 2008, 2012, 2017 Adrian Johnson <ajohnson@redneon.com>
// Copyright (C) 2008 Koji Otani <sho@bbr.jp>
// Copyright (C) 2008, 2010-2012, 2014-2022, 2024, 2025 Albert Astals Cid <aacid@kde.org>
// Copyright (C) 2008 Pino Toscano <pino@kde.org>
// Copyright (C) 2008, 2010 Hib Eris <hib@hiberis.nl>
// Copyright (C) 2009 Ross Moore <ross@maths.mq.edu.au>
// Copyright (C) 2009 Kovid Goyal <kovid@kovidgoyal.net>
// Copyright (C) 2010 Brian Ewins <brian.ewins@gmail.com>
// Copyright (C) 2010, 2021 Marek Kasik <mkasik@redhat.com>
// Copyright (C) 2010, 2020 Suzuki Toshiya <mpsuzuki@hiroshima-u.ac.jp>
// Copyright (C) 2011 Sam Liao <phyomh@gmail.com>
// Copyright (C) 2012 Horst Prote <prote@fmi.uni-stuttgart.de>
// Copyright (C) 2012, 2013-2018 Jason Crain <jason@aquaticape.us>
// Copyright (C) 2012 Peter Breitenlohner <peb@mppmu.mpg.de>
// Copyright (C) 2013 José Aliste <jaliste@src.gnome.org>
// Copyright (C) 2013 Thomas Freitag <Thomas.Freitag@alfa.de>
// Copyright (C) 2013 Ed Catmur <ed@catmur.co.uk>
// Copyright (C) 2016 Khaled Hosny <khaledhosny@eglug.org>
// Copyright (C) 2018 Klarälvdalens Datakonsult AB, a KDAB Group company, <info@kdab.com>. Work sponsored by the LiMux project of the city of Munich
// Copyright (C) 2018 Sanchit Anand <sanxchit@gmail.com>
// Copyright (C) 2018 Adam Reichold <adam.reichold@t-online.de>
// Copyright (C) 2018-2022, 2024 Nelson Benítez León <nbenitezl@gmail.com>
// Copyright (C) 2019 Christian Persch <chpe@src.gnome.org>
// Copyright (C) 2019, 2022 Oliver Sander <oliver.sander@tu-dresden.de>
// Copyright (C) 2019 Dan Shea <dan.shea@logical-innovations.com>
// Copyright (C) 2021 Peter Williams <peter@newton.cx>
// Copyright (C) 2024 Adam Sampson <ats@offog.org>
// Copyright (C) 2024, 2025 g10 Code GmbH, Author: Sune Stolborg Vuorela <sune@vuorela.dk>
// Copyright (C) 2024, 2025 Stefan Brüns <stefan.bruens@rwth-aachen.de>
//
// To see a description of the changes please see the Changelog file that
// came with your tarball or type make ChangeLog if you are building from git
//
//========================================================================

#include <config.h>

#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cmath>
#include <cfloat>
#include <cctype>
#include <algorithm>
#include <functional>
#if defined(_WIN32) || defined(__CYGWIN__)
#    include <fcntl.h> // for O_BINARY
#    include <io.h> // for _setmode
#endif
#include "goo/gfile.h"
#include "goo/gmem.h"
#include "goo/GooString.h"
#include "poppler-config.h"
#include "Error.h"
#include "GlobalParams.h"
#include "UnicodeMap.h"
#include "UnicodeTypeTable.h"
#include "Link.h"
#include "TextOutputDevOCR.h"
#include "Page.h"
#include "Annot.h"
#include "UTF.h"

// JDG
#include "splash/SplashBitmap.h"
#include "SplashOutputDev.h"
#include "PDFDoc.h"
#include <unordered_set>
#include "symmetric_matrix.h"

// For Unicode display
//#include <codecvt> // codecvt is depricated! See the utf8_to_string() function below for a quick, single character UTF8->std::string converstion

#include "random_forest.cpp" // <-- include the cpp file until the CMake scripts are updated!

//------------------------------------------------------------------------
// parameters
//------------------------------------------------------------------------

// Inter-character space width which will cause addChar to start a new
// word.
#define minWordBreakSpace 0.1

// Negative inter-character space width, i.e., overlap, which will
// cause addChar to start a new word.
#define minDupBreakOverlap 0.2

// Minimum inter-word spacing, as a fraction of the font size.
#define minWordSpacing 0.1 //0.15 <-- see 4ap2/PMC3597819/zbc7803.pdf for need to reduce

// Maximum inter-word spacing, as a fraction of the font size.
#define maxWordSpacing 0.9 // 1.5 <-- see table 1 in 4ap2/PMC3597819/zbc7803.pdf

// Maximum horizontal spacing which will allow a word to be pulled
// into a block, as a fraction of the font size.
// This default value can be tweaked via API.
double TextOutputDevOCR::minColSpacing1_default = 0.7;

// Max difference in primary,secondary coordinates (as a fraction of
// the font size) allowed for duplicated text (fake boldface, drop
// shadows) which is to be discarded.
#define dupMaxPriDelta 0.1
#define dupMaxSecDelta 0.2

// Max distance between characters when combining a base character and
// combining character
#define combMaxMidDelta 0.3
#define combMaxBaseDelta 0.4

// Text is considered diagonal if abs(tan(angle)) > diagonalThreshold.
// (Or 1/tan(angle) for 90/270 degrees.)
#define diagonalThreshold 0.1

// JDG -- a quick, single glyph replacement for the deprecated C++ codecvt_utf8 API
// See the wikipedia page for a definition of UTF-8: https://en.wikipedia.org/wiki/UTF-8
// This functionality is most likely provided by UTF.h/UTF.cc ... 
std::string utf8_to_string(Unicode m_code_point)
{
    std::string ret;

    ret.reserve(4);
    
    if(m_code_point < 0x80){ // One byte
        ret.push_back( char(m_code_point) );
    }
    else if(m_code_point < 0x800){ // Two byte
        ret.push_back( char( (3 << 6) | (m_code_point >> 6) ) );
        ret.push_back( char( (3 << 7) | (m_code_point & 0x3F) ) );
    }
    else if(m_code_point < 0x10000){ // Three byte
        ret.push_back( char( (7 << 5) | (m_code_point >> 12) ) );
        ret.push_back( char( (7 << 7) | ( (m_code_point >> 6) & 0x3F) ) );
        ret.push_back( char( (7 << 7) | (m_code_point & 0x3F) ) );
    }
    else{ // Four byte
        ret.push_back( char( (15 << 4) | (m_code_point >> 18) ) );
        ret.push_back( char( (15 << 7) | ( (m_code_point >> 12) & 0x3F) ) );
        ret.push_back( char( (15 << 7) | ( (m_code_point >> 6) & 0x3F) ) );
        ret.push_back( char( (15 << 7) | (m_code_point & 0x3F) ) );
    }

    return ret;
}

namespace {
    inline bool isAscii7(Unicode uchar)
    {
        return uchar < 128;
    }
}

static int reorderText(const Unicode *text, int len, const UnicodeMap *uMap, bool primaryLR, GooString *s, Unicode *u)
{
    char lre[8], rle[8], popdf[8], buf[8];
    int lreLen = 0, rleLen = 0, popdfLen = 0, n;
    int nCols, i, j, k;

    nCols = 0;

    if (s) {
        lreLen = uMap->mapUnicode(0x202a, lre, sizeof(lre));
        rleLen = uMap->mapUnicode(0x202b, rle, sizeof(rle));
        popdfLen = uMap->mapUnicode(0x202c, popdf, sizeof(popdf));
    }

    if (primaryLR) {
        i = 0;
        while (i < len) {
            // output a left-to-right section
            for (j = i; j < len && !unicodeTypeR(text[j]); ++j) {
                ;
            }
            for (k = i; k < j; ++k) {
                if (s) {
                    n = uMap->mapUnicode(text[k], buf, sizeof(buf));
                    s->append(buf, n);
                }
                if (u) {
                    u[nCols] = text[k];
                }
                ++nCols;
            }
            i = j;
            // output a right-to-left section
            for (j = i; j < len && !(unicodeTypeL(text[j]) || unicodeTypeNum(text[j])); ++j) {
                ;
            }
            if (j > i) {
                if (s) {
                    s->append(rle, rleLen);
                }
                for (k = j - 1; k >= i; --k) {
                    if (s) {
                        n = uMap->mapUnicode(text[k], buf, sizeof(buf));
                        s->append(buf, n);
                    }
                    if (u) {
                        u[nCols] = text[k];
                    }
                    ++nCols;
                }
                if (s) {
                    s->append(popdf, popdfLen);
                }
                i = j;
            }
        }
    } else {
        // Note: This code treats numeric characters (European and
        // Arabic/Indic) as left-to-right, which isn't strictly correct
        // (incurs extra LRE/POPDF pairs), but does produce correct
        // visual formatting.
        if (s) {
            s->append(rle, rleLen);
        }
        i = len - 1;
        while (i >= 0) {
            // output a right-to-left section
            for (j = i; j >= 0 && !(unicodeTypeL(text[j]) || unicodeTypeNum(text[j])); --j) {
                ;
            }
            for (k = i; k > j; --k) {
                if (s) {
                    n = uMap->mapUnicode(text[k], buf, sizeof(buf));
                    s->append(buf, n);
                }
                if (u) {
                    u[nCols] = text[k];
                }
                ++nCols;
            }
            i = j;
            // output a left-to-right section
            for (j = i; j >= 0 && !unicodeTypeR(text[j]); --j) {
                ;
            }
            if (j < i) {
                if (s) {
                    s->append(lre, lreLen);
                }
                for (k = j + 1; k <= i; ++k) {
                    if (s) {
                        n = uMap->mapUnicode(text[k], buf, sizeof(buf));
                        s->append(buf, n);
                    }
                    if (u) {
                        u[nCols] = text[k];
                    }
                    ++nCols;
                }
                if (s) {
                    s->append(popdf, popdfLen);
                }
                i = j;
            }
        }
        if (s) {
            s->append(popdf, popdfLen);
        }
    }

    return nCols;
}

//------------------------------------------------------------------------
// TextFontInfoOCR
//------------------------------------------------------------------------

TextFontInfoOCR::TextFontInfoOCR(const GfxState *state)
{
    gfxFont = state->getFont();

    if (gfxFont) {
        const std::optional<std::string> &gfxFontName = gfxFont->getName();
        if (gfxFontName) {
            fontName = new GooString(*gfxFontName);
        } else {
            fontName = nullptr;
        }
    } else {
        fontName = nullptr;
    }
    flags = gfxFont ? gfxFont->getFlags() : 0;
}

TextFontInfoOCR::~TextFontInfoOCR()
{
    delete fontName;
}

bool TextFontInfoOCR::matches(const GfxState *state) const
{
    return state->getFont() == gfxFont;
}

bool TextFontInfoOCR::matches(const TextFontInfoOCR *fontInfo) const
{
    return gfxFont == fontInfo->gfxFont;
}

bool TextFontInfoOCR::matches(const Ref *ref) const
{
    return gfxFont && (*(gfxFont->getID()) == *ref);
}

double TextFontInfoOCR::getAscent() const
{
    return gfxFont ? gfxFont->getAscent() : 0.95;
}

double TextFontInfoOCR::getDescent() const
{
    return gfxFont ? gfxFont->getDescent() : -0.35;
}

int TextFontInfoOCR::getWMode() const
{
    return gfxFont ? gfxFont->getWMode() : 0;
}

//------------------------------------------------------------------------
// TextWordOCR
//------------------------------------------------------------------------

TextWordOCR::TextWordOCR(const GfxState *state, int rotA, double fontSizeA)
{
    rot = rotA;
    fontSize = fontSizeA;
    spaceAfter = false;
    invisible = (state->getRender() == 3);
    valid = true; // TextWordOCRs are valid until marked for deletion 

    underlined = false;
}

TextWordOCR::~TextWordOCR() = default;

void TextWordOCR::addChar(const GfxState *state, TextFontInfoOCR *fontA, double x, double y, double dx, double dy, int charPosA, int charLen, CharCode c, Unicode u)
{
    chars.push_back( CharInfo(u, c, 0.0, fontA) );
    charPosEnd = charPosA + charLen;

    if (getLength() == 1) {
        setInitialBounds(fontA, x, y);
    }

    if (wMode) { // vertical writing mode
        // NB: the rotation value has been incremented by 1 (in
        // TextPage::beginWord()) for vertical writing mode
        switch (rot) {
        case 0:
            chars.back().edge = x - fontSize;
            xMax() = edgeEnd = x;
            break;
        case 1:
            chars.back().edge = y - fontSize;
            yMax() = edgeEnd = y;
            break;
        case 2:
            chars.back().edge = x + fontSize;
            xMin() = edgeEnd = x;
            break;
        case 3:
            chars.back().edge = y + fontSize;
            yMin() = edgeEnd = y;
            break;
        }
    } else { // horizontal writing mode
        switch (rot) {
        case 0:
            chars.back().edge = x;
            xMax() = edgeEnd = x + dx;
            break;
        case 1:
            chars.back().edge = y;
            yMax() = edgeEnd = y + dy;
            break;
        case 2:
            chars.back().edge = x;
            xMin() = edgeEnd = x + dx;
            break;
        case 3:
            chars.back().edge = y;
            yMin() = edgeEnd = y + dy;
            break;
        }
    }
}

void TextWordOCR::setInitialBounds(TextFontInfoOCR *fontA, double x, double y)
{
    double ascent = fontA->getAscent() * fontSize;
    double descent = fontA->getDescent() * fontSize;
    wMode = fontA->getWMode();

    if (wMode) { // vertical writing mode
        // NB: the rotation value has been incremented by 1 (in
        // TextPage::beginWord()) for vertical writing mode
        switch (rot) {
        case 0:
            xMin() = x - fontSize;
            yMin() = y - fontSize;
            yMax() = y;
            base = y;
            break;
        case 1:
            xMin() = x;
            yMin() = y - fontSize;
            xMax() = x + fontSize;
            base = x;
            break;
        case 2:
            yMin() = y;
            xMax() = x + fontSize;
            yMax() = y + fontSize;
            base = y;
            break;
        case 3:
            xMin() = x - fontSize;
            xMax() = x;
            yMax() = y + fontSize;
            base = x;
            break;
        }
    } else { // horizontal writing mode
        switch (rot) {
        case 0:
            xMin() = x;
            yMin() = y - ascent;
            yMax() = y - descent;
            if (yMin() == yMax()) {
                // this is a sanity check for a case that shouldn't happen -- but
                // if it does happen, we want to avoid dividing by zero later
                yMin() = y;
                yMax() = y + 1;
            }
            base = y;
            break;
        case 1:
            xMin() = x + descent;
            yMin() = y;
            xMax() = x + ascent;
            if (xMin() == xMax()) {
                // this is a sanity check for a case that shouldn't happen -- but
                // if it does happen, we want to avoid dividing by zero later
                xMin() = x;
                xMax() = x + 1;
            }
            base = x;
            break;
        case 2:
            yMin() = y + descent;
            xMax() = x;
            yMax() = y + ascent;
            if ( yMin() == yMax() ) {
                // this is a sanity check for a case that shouldn't happen -- but
                // if it does happen, we want to avoid dividing by zero later
                yMin() = y;
                yMax() = y + 1;
            }
            base = y;
            break;
        case 3:
            xMin() = x - ascent;
            xMax() = x - descent;
            yMax() = y;
            if ( xMin() == xMax() ) {
                // this is a sanity check for a case that shouldn't happen -- but
                // if it does happen, we want to avoid dividing by zero later
                xMin() = x;
                xMax() = x + 1;
            }
            base = x;
            break;
        }
    }
}

bool TextWordOCR::is_alphabetic_word() const
{
    // Does this word contain one or more, non-formatting metadata characters that match [a-z|A-Z]?
    for(std::deque<CharInfo>::const_iterator i = chars.begin();i != chars.end();++i){
        
        if( !i->is_formatting && ( ( ( i->text >= 0x41 /*A*/) && (i->text <= 0x5A /*Z*/) ) || 
                                   ( ( i->text >= 0x61 /*a*/) && (i->text <= 0x7A /*z*/) ) ) ){
            return true;
        }
    }

    return false;
};

struct CombiningTable
{
    Unicode base;
    Unicode comb;
};

static const struct CombiningTable combiningTable[] = {
    { 0x0060, 0x0300 }, // grave
    { 0x00a8, 0x0308 }, // dieresis
    { 0x00af, 0x0304 }, // macron
    { 0x00b4, 0x0301 }, // acute
    { 0x00b8, 0x0327 }, // cedilla
    { 0x02c6, 0x0302 }, // circumflex
    { 0x02c7, 0x030c }, // caron
    { 0x02d8, 0x0306 }, // breve
    { 0x02d9, 0x0307 }, // dotaccent
    { 0x02da, 0x030a }, // ring
    { 0x02dc, 0x0303 }, // tilde
    { 0x02dd, 0x030b } // hungarumlaut (double acute accent)
};

// returning combining versions of characters
static Unicode getCombiningChar(Unicode u)
{
    for (const CombiningTable &combining : combiningTable) {
        if (u == combining.base) {
            return combining.comb;
        }
    }
    return 0;
}

bool TextWordOCR::addCombining(const GfxState *state, TextFontInfoOCR *fontA, double fontSizeA, double x, double y, double dx, double dy, int charPosA, int charLen, CharCode c, Unicode u)
{
    if (chars.empty() || wMode != 0 || fontA->getWMode() != 0) {
        return false;
    }

    Unicode cCurrent = getCombiningChar(u);

    if (cCurrent != 0 && unicodeTypeAlphaNum(chars.back().text)) {

        // Current is a combining character, previous is base character
        double maxScaledMidDelta = fabs(edgeEnd - chars.back().edge) * combMaxMidDelta;
        double charMid, charBase, maxScaledBaseDelta;

        // Test if characters overlap
        if (rot == 0 || rot == 2) {
            charMid = x + (dx / 2);
            charBase = y;
            maxScaledBaseDelta = ( yMax() - yMin() ) * combMaxBaseDelta;
        } else {
            charMid = y + (dy / 2);
            charBase = x;
            maxScaledBaseDelta = ( xMax() - xMin() ) * combMaxBaseDelta;
        }

        double edgeMid = (chars.back().edge + edgeEnd) / 2;
        if (fabs(charMid - edgeMid) >= maxScaledMidDelta || fabs(charBase - base) >= maxScaledBaseDelta) {
            return false;
        }

        // Add character, but don't adjust edge / bounding box because
        // combining character's positioning could be odd.
        chars.emplace_back( CharInfo(cCurrent, c, edgeMid, fontA) );
        charPosEnd = charPosA + charLen;

        return true;
    }

    Unicode cPrev = getCombiningChar(chars.back().text);

    if (cPrev != 0 && unicodeTypeAlphaNum(u)) {

        // Previous is a combining character, current is base character
        double maxScaledBaseDelta = (fontA->getAscent() - fontA->getDescent()) * fontSizeA * combMaxBaseDelta;
        double charMid, charBase, maxScaledMidDelta;

        // Test if characters overlap
        if (rot == 0 || rot == 2) {
            charMid = x + (dx / 2);
            charBase = y;
            maxScaledMidDelta = fabs(dx * combMaxMidDelta);
        } else {
            charMid = y + (dy / 2);
            charBase = x;
            maxScaledMidDelta = fabs(dy * combMaxMidDelta);
        }

        double edgeMid = (chars.back().edge + edgeEnd) / 2;
        if (fabs(charMid - edgeMid) >= maxScaledMidDelta || fabs(charBase - base) >= maxScaledBaseDelta) {
            return false;
        }

        fontSize = fontSizeA;
        // move combining character to after base character
        chars.emplace_back( CharInfo(cPrev, chars.back().charcode, edgeMid, chars.back().font) );

        auto &lastChar = chars[chars.size() - 2];

        charPosEnd = charPosA + charLen;
        lastChar.text = u;
        lastChar.charcode = c;
        lastChar.font = fontA;

        if (getLength() == 2) {
            setInitialBounds(fontA, x, y);
        }

        // Updated edges / bounding box because we changed the base
        // character.
        if (wMode) {
            // FIXME unreachable, wMode == 0
            switch (rot) {
            case 0:
                lastChar.edge = x - fontSize;
                xMax() = edgeEnd = x;
                break;
            case 1:
                lastChar.edge = y - fontSize;
                yMax() = edgeEnd = y;
                break;
            case 2:
                lastChar.edge = x + fontSize;
                xMin() = edgeEnd = x;
                break;
            case 3:
                lastChar.edge = y + fontSize;
                yMin() = edgeEnd = y;
                break;
            }
        } else {
            switch (rot) {
            case 0:
                lastChar.edge = x;
                xMax() = edgeEnd = x + dx;
                break;
            case 1:
                lastChar.edge = y;
                yMax() = edgeEnd = y + dy;
                break;
            case 2:
                lastChar.edge = x;
                xMin() = edgeEnd = x + dx;
                break;
            case 3:
                lastChar.edge = y;
                yMin() = edgeEnd = y + dy;
                break;
            }
        }

        chars.back().edge = (edgeEnd + lastChar.edge) / 2;
        return true;
    }
    return false;
}

GooString *TextWordOCR::getText() const
{
    GooString *s;
    const UnicodeMap *uMap;
    char buf[8];

    s = new GooString();
    if (!(uMap = globalParams->getTextEncoding())) {
        return s;
    }
    for (size_t i = 0; i < getLength(); ++i) {
        auto n = uMap->mapUnicode(chars[i].text, buf, sizeof(buf));
        s->append(buf, n);
    }
    return s;
}

//------------------------------------------------------------------------
// TextPageOCR
//------------------------------------------------------------------------

TextPageOCR::TextPageOCR(bool discardDiagA)
{
    refCnt = 1;
    discardDiag = discardDiagA;
    curWord = nullptr;
    charPos = 0;
    curFont = nullptr;
    curFontSize = 0;
    nest = 0;
    nTinyChars = 0;
    lastCharOverlap = false;
    
    lastFindXMin = lastFindYMin = 0;
    haveLastFind = false;
    mergeCombining = true;
    diagonal = false;
}

TextPageOCR::~TextPageOCR()
{
    clear();
}

void TextPageOCR::incRefCnt()
{
    refCnt++;
}

void TextPageOCR::decRefCnt()
{
    if (--refCnt == 0) {
        delete this;
    }
}

void TextPageOCR::startPage(const GfxState *state)
{
    clear();
    if (state) {
        pageWidth = state->getPageWidth();
        pageHeight = state->getPageHeight();
    } else {
        pageWidth = pageHeight = 0;
    }
}

void TextPageOCR::endPage()
{
    if (curWord) {
        endWord();
    }
}

void TextPageOCR::clear()
{
    if (curWord) {
        delete curWord;
        curWord = nullptr;
    }

    fonts.clear();
    clusters.clear();

    for(std::list<TextWordOCR*>::iterator w = ptr_pool.begin();w != ptr_pool.end();++w){
        delete (*w); // Frees the TextWordOCR object
    }

    ptr_pool.clear(); // <-- Deletes the list of pointers

    diagonal = false;
    curWord = nullptr;
    charPos = 0;
    curFont = nullptr;
    curFontSize = 0;
    nest = 0;
    nTinyChars = 0;
}

void TextPageOCR::updateFont(const GfxState *state)
{
    const double *fm;
    const char *name;
    int code, mCode, letterCode, anyCode;
    double w;

    // get the font info object
    curFont = nullptr;
    for (const std::unique_ptr<TextFontInfoOCR> &f : fonts) {
        if (f->matches(state)) {
            curFont = f.get();
            break;
        }
    }
    if (!curFont) {
        fonts.emplace_back(std::make_unique<TextFontInfoOCR>(state));
        curFont = fonts.back().get();
    }

    // adjust the font size
    GfxFont *const gfxFont = state->getFont().get();
    curFontSize = state->getTransformedFontSize();
    if (gfxFont && gfxFont->getType() == fontType3) {
        // This is a hack which makes it possible to deal with some Type 3
        // fonts.  The problem is that it's impossible to know what the
        // base coordinate system used in the font is without actually
        // rendering the font.  This code tries to guess by looking at the
        // width of the character 'm' (which breaks if the font is a
        // subset that doesn't contain 'm').
        mCode = letterCode = anyCode = -1;
        for (code = 0; code < 256; ++code) {
            name = ((Gfx8BitFont *)gfxFont)->getCharName(code);
            int nameLen = name ? strlen(name) : 0;
            bool nameOneChar = nameLen == 1 || (nameLen > 1 && name[1] == '\0');
            if (nameOneChar && name[0] == 'm') {
                mCode = code;
            }
            if (letterCode < 0 && nameOneChar && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z'))) {
                letterCode = code;
            }
            if (anyCode < 0 && name && ((Gfx8BitFont *)gfxFont)->getWidth(code) > 0) {
                anyCode = code;
            }
        }
        if (mCode >= 0 && (w = ((Gfx8BitFont *)gfxFont)->getWidth(mCode)) > 0) {
            // 0.6 is a generic average 'm' width -- yes, this is a hack
            curFontSize *= w / 0.6;
        } else if (letterCode >= 0 && (w = ((Gfx8BitFont *)gfxFont)->getWidth(letterCode)) > 0) {
            // even more of a hack: 0.5 is a generic letter width
            curFontSize *= w / 0.5;
        } else if (anyCode >= 0 && (w = ((Gfx8BitFont *)gfxFont)->getWidth(anyCode)) > 0) {
            // better than nothing: 0.5 is a generic character width
            curFontSize *= w / 0.5;
        }
        fm = gfxFont->getFontMatrix();
        if (fm[0] != 0) {
            curFontSize *= fabs(fm[3] / fm[0]);
        }
    }
}

void TextPageOCR::beginWord(const GfxState *state)
{
    const double *fontm;
    double m[4], m2[4];
    int rot;

    // This check is needed because Type 3 characters can contain
    // text-drawing operations (when TextPage is being used via
    // {X,Win}SplashOutputDev rather than TextOutputDev).
    if (curWord) {
        ++nest;
        return;
    }

    // compute the rotation
    state->getFontTransMat(&m[0], &m[1], &m[2], &m[3]);
    const std::shared_ptr<GfxFont> &gfxFont = state->getFont();
    if (gfxFont && gfxFont->getType() == fontType3) {
        fontm = state->getFont()->getFontMatrix();
        m2[0] = fontm[0] * m[0] + fontm[1] * m[2];
        m2[1] = fontm[0] * m[1] + fontm[1] * m[3];
        m2[2] = fontm[2] * m[0] + fontm[3] * m[2];
        m2[3] = fontm[2] * m[1] + fontm[3] * m[3];
        m[0] = m2[0];
        m[1] = m2[1];
        m[2] = m2[2];
        m[3] = m2[3];
    }
    if (fabs(m[0] * m[3]) > fabs(m[1] * m[2])) {
        rot = (m[0] > 0 || m[3] < 0) ? 0 : 2;
    } else {
        rot = (m[2] > 0) ? 1 : 3;
    }
    if (fabs(m[0]) >= fabs(m[1])) {
        diagonal = fabs(m[1]) > diagonalThreshold * fabs(m[0]);
    } else {
        diagonal = fabs(m[0]) > diagonalThreshold * fabs(m[1]);
    }

    // for vertical writing mode, the lines are effectively rotated 90
    // degrees
    if (gfxFont && gfxFont->getWMode()) {
        rot = (rot + 1) & 3;
    }

    curWord = new TextWordOCR(state, rot, curFontSize);
}

void TextPageOCR::addChar(const GfxState *state, double x, double y, double dx, double dy, CharCode c, int nBytes, const Unicode *u, int uLen)
{
    // Added the prefix "local_" to x1 and y1 to avoid shadowing the inherited x1 and y1 variables
    double local_x1, local_y1, w1, h1, dx2, dy2, base, sp, delta;
    bool overlap;
    int wMode;

    // subtract char and word spacing from the dx,dy values
    sp = state->getCharSpace();
    if (c == (CharCode)0x20) {
        sp += state->getWordSpace();
    }
    state->textTransformDelta(sp * state->getHorizScaling(), 0, &dx2, &dy2);
    dx -= dx2;
    dy -= dy2;
    state->transformDelta(dx, dy, &w1, &h1);

    // throw away chars that aren't inside the page bounds
    // (and also do a sanity check on the character size)
    state->transform(x, y, &local_x1, &local_y1);
    if (local_x1 + w1 < 0 || local_x1 > pageWidth || local_y1 + h1 < 0 || local_y1 > pageHeight || std::isnan(local_x1) || std::isnan(local_y1) || std::isnan(w1) || std::isnan(h1)) {
        charPos += nBytes;
        return;
    }

    // check the tiny chars limit
    if (fabs(w1) < 3 && fabs(h1) < 3) {
        if (++nTinyChars > 50000) {
            charPos += nBytes;
            return;
        }
    }

    // break words at space character
    if (uLen == 1 && UnicodeIsWhitespace(u[0])) {
        charPos += nBytes;
        endWord();
        return;
    } else if (uLen == 1 && u[0] == (Unicode)0x0) {
        // ignore null characters
        charPos += nBytes;
        return;
    }

    if (mergeCombining && curWord && uLen == 1 && curWord->addCombining(state, curFont, curFontSize, local_x1, local_y1, w1, h1, charPos, nBytes, c, u[0])) {

        charPos += nBytes;
        return;
    }

    // start a new word if:
    // (1) this character doesn't fall in the right place relative to
    //     the end of the previous word (this places upper and lower
    //     constraints on the position deltas along both the primary
    //     and secondary axes), or
    // (2) this character overlaps the previous one (duplicated text), or
    // (3) the previous character was an overlap (we want each duplicated
    //     character to be in a word by itself at this stage),
    // (4) the font size has changed
    // (5) the WMode changed
    if (curWord && curWord->getLength() > 0) {
        base = sp = delta = 0; // make gcc happy
        switch (curWord->rot) {
        case 0:
            base = local_y1;
            sp = local_x1 - curWord->xMax();
            delta = local_x1 - curWord->chars.back().edge;
            break;
        case 1:
            base = local_x1;
            sp = local_y1 - curWord->yMax();
            delta = local_y1 - curWord->chars.back().edge;
            break;
        case 2:
            base = local_y1;
            sp = curWord->xMin() - local_x1;
            delta = curWord->chars.back().edge - local_x1;
            break;
        case 3:
            base = local_x1;
            sp = curWord->yMin() - local_y1;
            delta = curWord->chars.back().edge - local_y1;
            break;
        }
        overlap = fabs(delta) < dupMaxPriDelta * curWord->fontSize && fabs(base - curWord->base) < dupMaxSecDelta * curWord->fontSize;
        wMode = curFont->getWMode();
        if (overlap || lastCharOverlap || sp < -minDupBreakOverlap * curWord->fontSize || sp > minWordBreakSpace * curWord->fontSize || fabs(base - curWord->base) > 0.5 || curFontSize != curWord->fontSize || wMode != curWord->wMode) {
            endWord();
        }
        lastCharOverlap = overlap;
    } else {
        lastCharOverlap = false;
    }

    if (uLen != 0) {
        // start a new word if needed
        if (!curWord) {
            beginWord(state);
        }

        // throw away diagonal chars
        if (discardDiag && diagonal) {
            charPos += nBytes;
            return;
        }

        // page rotation and/or transform matrices can cause text to be
        // drawn in reverse order -- in this case, swap the begin/end
        // coordinates and break text into individual chars
        if ((curWord->rot == 0 && w1 < 0) || (curWord->rot == 1 && h1 < 0) || (curWord->rot == 2 && w1 > 0) || (curWord->rot == 3 && h1 > 0)) {
            endWord();
            beginWord(state);

            // throw away diagonal chars
            if (discardDiag && diagonal) {
                charPos += nBytes;
                return;
            }

            local_x1 += w1;
            local_y1 += h1;
            w1 = -w1;
            h1 = -h1;
        }

        // add the characters to the current word
        w1 /= uLen;
        h1 /= uLen;
        for (int i = 0; i < uLen; ++i) {
            curWord->addChar(state, curFont, local_x1 + i * w1, local_y1 + i * h1, w1, h1, charPos, nBytes, c, u[i]);
        }
    }
    charPos += nBytes;
}

void TextPageOCR::incCharCount(int nChars)
{
    charPos += nChars;
}

void TextPageOCR::endWord()
{
    // This check is needed because Type 3 characters can contain
    // text-drawing operations (when TextPage is being used via
    // {X,Win}SplashOutputDev rather than TextOutputDev).
    if (nest > 0) {
        --nest;
        return;
    }

    if (curWord) {
        addWord(curWord);
        curWord = nullptr;
    }
}

void TextPageOCR::addWord(TextWordOCR *word)
{
    // throw away zero-length words -- they don't have valid xMin/xMax
    // values, and they're useless anyway
    if (word->getLength() == 0) {
        delete word;
        return;
    }
    
    ptr_pool.push_back(word);
}

void TextPageOCR::coalesce()
{
    coalesce(TextOutputDevOCR::minColSpacing1_default);
}

void TextPageOCR::coalesce(double minColSpacing1)
{
    primaryRot = 0;
    
    // Convert subscript and superscript characters to inline representations
    coalesce_superscript_and_subscript();

    primaryLR = true; // The scientific literature is almost 100% left-to-right
    cluster_words();
}

typedef enum{LEFT_SUBSCRIPT, LEFT_SUPERSCRIPT,  RIGHT_SUBSCRIPT, RIGHT_SUPERSCRIPT} ScriptLocation;

struct sort_by_script_distance
{
    inline bool operator()(const tuple<double /*distance*/, TextWordOCR * /*target*/, TextWordOCR * /*script*/, ScriptLocation> &m_a,
        const tuple<double /*distance*/, TextWordOCR * /*target*/, TextWordOCR * /*script*/, ScriptLocation> &m_b) const
    {
        return get<0>(m_a) < get<0>(m_b);
    };
};

// Modify the word pools by merging subscript and superscript text into the associated target words
// using HTML <sub>xxx</sub> and <sup>xxx</sup> notation
void TextPageOCR::coalesce_superscript_and_subscript()
{
    if(pageWidth <= 0.0){
        cerr << "TextPageOCR::coalesce_superscript_and_subscript: Invalid pageWidth" << endl;
        return;
    }

    if(pageHeight <= 0.0){
        cerr << "TextPageOCR::coalesce_superscript_and_subscript: Invalid pageHeight" << endl;
        return;
    }

    // Subscripted and superscripted glyphs are observed to paritally overlap in x
    #define     SCRIPT_DX_MIN_THRESHOLD         -0.2 // As a fraction of font size
    #define     SCRIPT_DY_MIN_THRESHOLD         0.1 // As a fraction of font size

    #define     SCRIPT_DX_MAX_THRESHOLD         0.75 // As a fraction of font size
    #define     SCRIPT_DY_MAX_THRESHOLD         0.75 //0.5 // As a fraction of font size

    #define     RIGHT_PREFERENCE_DISTANCE_FUDGE_FACTOR   0.99 // Reduce the observed distances for right superscripts and subsripts
                                                              // to break ties (and approximate ties) with left superscripts and subscripts
    const int SEARCH_GRID_DIM_X = (pageWidth < pageHeight) ? 10 : 15;
    const int SEARCH_GRID_DIM_Y = (pageWidth < pageHeight) ? 15 : 10;
    
    // "Drop case" refers to the large capital letter that is often used to start the text of a document. Unfortunately, drop case confuses the
    // subscript and superscript detection algorithm!
    const double drop_case_min_font_size = 20.0;

    const string subscript_prefix = "<sub>";
    const string subscript_suffix = "</sub>";

    const string superscript_prefix = "<sup>";
    const string superscript_suffix = "</sup>";

    const double grid_dx = pageWidth/SEARCH_GRID_DIM_X;
    const double grid_dy = pageHeight/SEARCH_GRID_DIM_Y;

    #define DISTANCE2_LEFT(TARGET_BOX, SCRIPT_BOX) \
        ( pow(TARGET_BOX.x1 - SCRIPT_BOX.x2, 2.0) + pow(0.5*( TARGET_BOX.y1 + TARGET_BOX.y2 - SCRIPT_BOX.y1 - SCRIPT_BOX.y2), 2.0) )

    #define DISTANCE2_RIGHT(TARGET_BOX, SCRIPT_BOX) \
        ( RIGHT_PREFERENCE_DISTANCE_FUDGE_FACTOR * ( pow(TARGET_BOX.x2 - SCRIPT_BOX.x1, 2.0) + pow(0.5*(TARGET_BOX.y1 + TARGET_BOX.y2 - SCRIPT_BOX.y1 - SCRIPT_BOX.y2), 2.0) ) )

    vector< vector< deque<TextWordOCR *> > > grid( SEARCH_GRID_DIM_X, vector< deque<TextWordOCR *> >(SEARCH_GRID_DIM_Y) );

    // Store all potential target + superscript and target + subscript matches so we can sort by match distance
    deque< tuple<double /*distance*/, TextWordOCR * /*target*/, TextWordOCR * /*script*/, ScriptLocation> > candidates;
    
    for(std::list<TextWordOCR*>::iterator w = ptr_pool.begin();w != ptr_pool.end();++w){
            
        if( (*w)->valid == false){
            continue;
        }

        // Each word has a bounding box and we need to add each of the four bounding box corners to the grid

        // Upper left corner
        int grid_x = min(int( (*w)->xMin()/grid_dx ), SEARCH_GRID_DIM_X - 1);
        int grid_y = min(int( (*w)->yMin()/grid_dy ), SEARCH_GRID_DIM_Y - 1);

        grid[grid_x][grid_y].push_back( *w );

        // Lower left corner
        grid_x = min(int( (*w)->xMin()/grid_dx ), SEARCH_GRID_DIM_X - 1);
        grid_y = min(int( (*w)->yMax()/grid_dy ), SEARCH_GRID_DIM_Y - 1);

        grid[grid_x][grid_y].push_back( *w );

        // Upper right corner
        grid_x = min(int( (*w)->xMax()/grid_dx ), SEARCH_GRID_DIM_X - 1);
        grid_y = min(int( (*w)->yMin()/grid_dy ), SEARCH_GRID_DIM_Y - 1);

        grid[grid_x][grid_y].push_back( *w );

        // Lower right corner
        grid_x = min(int( (*w)->xMax()/grid_dx ), SEARCH_GRID_DIM_X - 1);
        grid_y = min(int( (*w)->yMax()/grid_dy ), SEARCH_GRID_DIM_Y - 1);

        grid[grid_x][grid_y].push_back( *w );
    }

    // Count the number of unique, valid script string pointers (subscript or superscript in any location) found for each target string
    // Since there can be "giant" strings that span multiple grid elements, we need to accumulate the count over all
    // grid elements.
    unordered_map< TextWordOCR *, unordered_set<TextWordOCR *> > script_count;
    
    // Seach the word grid to find nearest neighbors of each word corner
    for(int grid_x = 0;grid_x < SEARCH_GRID_DIM_X;++grid_x){
        for(int grid_y = 0;grid_y < SEARCH_GRID_DIM_Y;++grid_y){

            const deque<TextWordOCR *>::iterator begin_target = grid[grid_x][grid_y].begin();
            const deque<TextWordOCR *>::iterator end_target = grid[grid_x][grid_y].end();

            for(deque<TextWordOCR *>::iterator iter_target = begin_target;iter_target != end_target;++iter_target){

                if( (*iter_target)->valid == false){
                    continue;
                }
                
                if( (*iter_target)->chars.empty() ){
                    continue;
                }

                if( ( (*iter_target)->fontSize >= drop_case_min_font_size ) && ( (*iter_target)->chars.size() == 1 ) && isupper((*iter_target)->chars.front().charcode) ){
                    continue; // This is likely a drop case character
                }

                const PDFRectangle delta(
                    SCRIPT_DX_MIN_THRESHOLD * ( (*iter_target)->fontSize ), // Subscript and superscripted glyphs are observed to paritally overlap in x
                    SCRIPT_DY_MIN_THRESHOLD * ( (*iter_target)->fontSize ),
                    SCRIPT_DX_MAX_THRESHOLD * ( (*iter_target)->fontSize ),
                    SCRIPT_DY_MAX_THRESHOLD * ( (*iter_target)->fontSize )
                );
                
                PDFRectangle target_box = *(*iter_target);

                // Undo the target word rotation
                if( (*iter_target)->rot != 0 ){
                    target_box = target_box.rotate_counter_clockwise( (*iter_target)->rot );
                }

                // Store the best subscript and superscript candidates here
                TextWordOCR *left_subscript_ptr = NULL;
                double left_subscript_distance = std::numeric_limits<double>::max();

                TextWordOCR *left_superscript_ptr = NULL;
                double left_superscript_distance = std::numeric_limits<double>::max();

                TextWordOCR *right_subscript_ptr = NULL;
                double right_subscript_distance = std::numeric_limits<double>::max();

                TextWordOCR *right_superscript_ptr = NULL;
                double right_superscript_distance = std::numeric_limits<double>::max();

                for(int grid_offset_x = -1;grid_offset_x <= 1;++grid_offset_x){
                        
                    if( ( (grid_x + grid_offset_x) < 0 ) || ( (grid_x + grid_offset_x) >= SEARCH_GRID_DIM_X ) ){
                        continue;
                    }

                    for(int grid_offset_y = -1;grid_offset_y <= 1;++grid_offset_y){
                        
                        if( ( (grid_y + grid_offset_y) < 0 ) || ( (grid_y + grid_offset_y) >= SEARCH_GRID_DIM_Y ) ){
                            continue;
                        }

                        const deque<TextWordOCR *>::iterator begin_script = grid[grid_x + grid_offset_x][grid_y + grid_offset_y].begin();
                        const deque<TextWordOCR *>::iterator end_script = grid[grid_x + grid_offset_x][grid_y + grid_offset_y].end();

                        for(deque<TextWordOCR *>::iterator iter_script = begin_script;iter_script != end_script;++iter_script){

                            if( (*iter_target == *iter_script) || ( (*iter_script)->valid == false) ){
                                continue; // Don't compare a word to itself
                            }

                            // Require subscripts and superscripts to have smaller (or equal) font sizes than the target word
                            //if( (*iter_script)->fontSize >= (*iter_target)->fontSize){ <-- Doesn't catch single character scripts (like * and dagger) that may have the same font size
                            if( (*iter_script)->fontSize > (*iter_target)->fontSize ){
                                continue;
                            }
                            
                            if( ( (*iter_script)->fontSize == (*iter_target)->fontSize ) && ( (*iter_script)->getLength() > (*iter_target)->getLength() ) ){
                                // Allow single character scripts (like * and dagger) that may have the same font size, but require that the scripted text
                                // be shorter or equal to the target text. If this proves to be too fragile, we can supplement/replace with test that compares
                                // the frequency of word baselines (and requires the script baseline to be lower frequency)
                                continue;
                            }

                            // Require subscripts and superscripts to have the same rotation
                            if((*iter_script)->rot != (*iter_target)->rot){
                                continue;
                            }

                            PDFRectangle script_box = *(*iter_script);

                            // Undo the script word rotation
                            if( (*iter_script)->rot != 0 ){
                                script_box = script_box.rotate_counter_clockwise( (*iter_script)->rot );
                            }

                            //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
                            // Left subscript
                            double d = DISTANCE2_LEFT(target_box, script_box);
                            double dx = target_box.x1 - script_box.x2;
                            double dy = (*iter_script)->getBaseline() - (*iter_target)->getBaseline(); //script_box.ave_y() - target_box.ave_y();

                            if( (dx >= delta.x1) && (dy >= delta.y1) && (dx <= delta.x2) && (dy <= delta.y2) ){

                                script_count[*iter_target].insert(*iter_script);

                                if(left_subscript_distance > d){
                                    
                                    left_subscript_ptr = *iter_script;
                                    left_subscript_distance = d;
                                }
                            }

                            //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
                            // Left superscript
                            d = DISTANCE2_LEFT(target_box, script_box);
                            dx = target_box.x1 - script_box.x2;
                            dy = (*iter_target)->getBaseline() - (*iter_script)->getBaseline();//target_box.ave_y() - script_box.ave_y();

                            // DEBUG
                            //if( (script_box.x1 < 92) && (script_box.x2 > 92) && (script_box.y1 > 700) ){
                            //    cout << "left super: " << (*iter_script)->getText()->toStr() << "<-" << (*iter_target)->getText()->toStr() << "; dx = " << dx << "; dy = " << dy 
                            //        << ";script " << script_box.x1 << ".." << script_box.x2 << " " << script_box.y1 << ".." << script_box.y2 << endl;  
                            //}
                            //cout << "left super: " << (*iter_script)->getText()->toStr() << "<-" << (*iter_target)->getText()->toStr() << "; dx = " << dx << "; dy = " << dy << endl;

                            if( (dx >= delta.x1) && (dy >= delta.y1) && (dx <= delta.x2) && (dy <= delta.y2) ){

                                script_count[*iter_target].insert(*iter_script);

                                if(left_superscript_distance > d){

                                    // DEBUG
                                    //cout << "# delta.x1 = " << delta.x1 << ";delta.y1 = " << delta.y1 << ";delta.x2 = " << delta.x2 << ";delta.y2 = " << delta.y2 << endl;
                                    //cout << "left super: " << (*iter_script)->getText()->toStr() << "<-" << (*iter_target)->getText()->toStr() 
                                    //    << ";dx = " << dx << ";dy = " << dy 
                                    //    << ";script " << script_box.x1 << ".." << script_box.x2 << " " << script_box.y1 << ".." << script_box.y2 
                                    //    << ";target " << target_box.x1 << ".." << target_box.x2 << " " << target_box.y1 << ".." << target_box.y2 
                                    //    << ";script baseline = " << (*iter_script)->getBaseline() << ";target baseline = " << (*iter_target)->getBaseline() 
                                    //    << ";script font = " << (*iter_script)->fontSize << ";target font = " << (*iter_target)->fontSize << endl;  

                                    left_superscript_ptr = *iter_script;
                                    left_superscript_distance = d;
                                }
                            }

                            //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
                            // Right subscript
                            d = DISTANCE2_RIGHT(target_box, script_box);
                            dx = script_box.x1 - target_box.x2;
                            dy = (*iter_script)->getBaseline() - (*iter_target)->getBaseline();//script_box.ave_y() - target_box.ave_y();

                            if( (dx >= delta.x1) && (dy >= delta.y1) && (dx <= delta.x2) && (dy <= delta.y2) ){

                                script_count[*iter_target].insert(*iter_script);

                                if(right_subscript_distance > d){

                                    right_subscript_ptr = *iter_script;
                                    right_subscript_distance = d;
                                }
                            }

                            //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
                            // Right superscript
                            d = DISTANCE2_RIGHT(target_box, script_box);
                            dx = script_box.x1 - target_box.x2;
                            dy = (*iter_target)->getBaseline() - (*iter_script)->getBaseline(); //target_box.ave_y() - script_box.ave_y();

                            if( (dx >= delta.x1) && (dy >= delta.y1) && (dx <= delta.x2) && (dy <= delta.y2) ){

                                script_count[*iter_target].insert(*iter_script);

                                if(right_superscript_distance > d){

                                    right_superscript_ptr = *iter_script;
                                    right_superscript_distance = d;
                                }
                            }
                        }
                    }
                }

                // Do we have valid superscript/subscript candidates?
                if(left_subscript_ptr && left_subscript_ptr->valid){
                    candidates.push_back( make_tuple(left_subscript_distance, *iter_target, left_subscript_ptr, LEFT_SUBSCRIPT) );
                }

                if(left_superscript_ptr && left_superscript_ptr->valid){
                    candidates.push_back( make_tuple(left_superscript_distance, *iter_target, left_superscript_ptr, LEFT_SUPERSCRIPT) );
                }

                if(right_subscript_ptr && right_subscript_ptr->valid){
                    candidates.push_back( make_tuple(right_subscript_distance, *iter_target, right_subscript_ptr, RIGHT_SUBSCRIPT) );
                }

                if(right_superscript_ptr && right_superscript_ptr->valid){
                    candidates.push_back( make_tuple(right_superscript_distance, *iter_target, right_superscript_ptr, RIGHT_SUPERSCRIPT) );
                }
            }
        }
    }

    // DEBUG
    //cerr << "rot = " << rot << "; |candidates| = " << candidates.size() << endl;

    // Sort the candidates by distance
    sort( candidates.begin(), candidates.end(), sort_by_script_distance() );

    for( deque< tuple<double /*distance*/, TextWordOCR * /*target*/, TextWordOCR * /*script*/, ScriptLocation> >::iterator iter = candidates.begin();iter != candidates.end();++iter){

        TextWordOCR *target_ptr = get<1>(*iter);
        TextWordOCR *script_ptr = get<2>(*iter);

        // Skip invalid words (that have already been included in a previous subscript/superscript realtionship)
        if( (target_ptr == NULL) || (script_ptr == NULL) || (target_ptr->valid == false) || (script_ptr->valid == false) ){
            continue;
        }

        unordered_map< TextWordOCR *, unordered_set<TextWordOCR *> >::const_iterator count_iter = script_count.find(target_ptr);

        // Skip target words that have more than four unique valid superscript and/or subscripts
        if( ( count_iter != script_count.end() ) && (count_iter->second.size() > 4) ){
            continue;
        }

        // We have a valid target + script relationship!
        const TextWordOCR::CharInfo reference_char = target_ptr->chars.front();
        
        script_ptr->valid = false; // Mark the script for future deletion

        switch( get<3>(*iter) ){
            case LEFT_SUBSCRIPT:
                {
                    // DEBUG
                    //cerr << "LEFT_SUBSCRIPT (" << get<0>(*iter) << ") " << script_ptr->getText()->toStr() << "_" << target_ptr->getText()->toStr() 
                    //    << "; target size = " << target_ptr->fontSize << "; script size = " << script_ptr->fontSize << "; count = " << count_iter->second.size() << endl;

                    for(string::const_reverse_iterator i = subscript_suffix.rbegin();i != subscript_suffix.rend();++i){
                        target_ptr->chars.push_front( TextWordOCR::CharInfo( *i, *i, reference_char.edge, reference_char.font, true /*formatting metadata*/) );

                    }

                    for(std::deque<TextWordOCR::CharInfo>::const_reverse_iterator i = script_ptr->chars.rbegin();i != script_ptr->chars.rend();++i){
                        target_ptr->chars.push_front( TextWordOCR::CharInfo( i->text, i->charcode, reference_char.edge, reference_char.font, false) );
                    }

                    for(string::const_reverse_iterator i = subscript_prefix.rbegin();i != subscript_prefix.rend();++i){
                        target_ptr->chars.push_front( TextWordOCR::CharInfo( *i, *i, reference_char.edge, reference_char.font, true /*formatting metadata*/) );
                    }

                    // Include the script in the target bounding box
                    switch(target_ptr->rot){
                        case 0:
                            target_ptr->xMin() = script_ptr->xMin();
                            break;
                        case 1:
                            target_ptr->yMin() = script_ptr->yMin();
                            break;
                        case 2:
                            target_ptr->xMax() = script_ptr->xMax();
                            break;
                        case 3:
                            target_ptr->yMax() = script_ptr->yMax();
                            break;
                        default:
                            error(errInternal, -1, __FILE__ ":TextPageOCR::coalesce_superscript_and_subscript: Invalid rotation");
                    };
                }
                break;
            case LEFT_SUPERSCRIPT:
                {
                    // DEBUG
                    //cerr << "LEFT_SUPERSCRIPT (" << get<0>(*iter) << ") " << script_ptr->getText()->toStr() << "^" << target_ptr->getText()->toStr() 
                    //    << "; target size = " << target_ptr->fontSize << "; script size = " << script_ptr->fontSize << "; count = " << count_iter->second.size() << endl;

                    for(string::const_reverse_iterator i = superscript_suffix.rbegin();i != superscript_suffix.rend();++i){
                        target_ptr->chars.push_front( TextWordOCR::CharInfo( *i, *i, reference_char.edge, reference_char.font, true /*formatting metadata*/) );
                    }

                    for(std::deque<TextWordOCR::CharInfo>::const_reverse_iterator i = script_ptr->chars.rbegin();i != script_ptr->chars.rend();++i){
                        target_ptr->chars.push_front( TextWordOCR::CharInfo( i->text, i->charcode, reference_char.edge, reference_char.font, false) );
                    }

                    for(string::const_reverse_iterator i = superscript_prefix.rbegin();i != superscript_prefix.rend();++i){
                        target_ptr->chars.push_front( TextWordOCR::CharInfo( *i, *i, reference_char.edge, reference_char.font, true /*formatting metadata*/) );
                    }

                    // Include the script in the target bounding box
                    switch(target_ptr->rot){
                        case 0:
                            target_ptr->xMin() = script_ptr->xMin();
                            break;
                        case 1:
                            target_ptr->yMin() = script_ptr->yMin();
                            break;
                        case 2:
                            target_ptr->xMax() = script_ptr->xMax();
                            break;
                        case 3:
                            target_ptr->yMax() = script_ptr->yMax();
                            break;
                        default:
                           error(errInternal, -1, __FILE__ ":TextPageOCR::coalesce_superscript_and_subscript: Invalid rotation");
                    };
                }
                break;
            case RIGHT_SUBSCRIPT:
                {
                    // DEBUG
                    //cerr << "RIGHT_SUBSCRIPT (" << get<0>(*iter) << ") " << target_ptr->getText()->toStr() << "_" << script_ptr->getText()->toStr() 
                    //    << "; target size = " << target_ptr->fontSize << ";target baseline = " << target_ptr->getBaseline() 
                    //    << "; script size = " << script_ptr->fontSize << "; script baseline = " << script_ptr->getBaseline() << "; count = " << count_iter->second.size() << endl;

                    for(string::const_iterator i = subscript_prefix.begin();i != subscript_prefix.end();++i){
                        target_ptr->chars.push_back( TextWordOCR::CharInfo( *i, *i, reference_char.edge, reference_char.font, true /*formatting metadata*/) );
                    }

                    for(std::deque<TextWordOCR::CharInfo>::const_iterator i = script_ptr->chars.begin();i != script_ptr->chars.end();++i){
                        target_ptr->chars.push_back( TextWordOCR::CharInfo( i->text, i->charcode, reference_char.edge, reference_char.font, false) );
                    }

                    for(string::const_iterator i = subscript_suffix.begin();i != subscript_suffix.end();++i){
                        target_ptr->chars.push_back( TextWordOCR::CharInfo( *i, *i, reference_char.edge, reference_char.font, true /*formatting metadata*/) );
                    }

                    // Include the script in the target bounding box
                    switch(target_ptr->rot){
                        case 0:
                            target_ptr->xMax() = script_ptr->xMax();
                            break;
                        case 1:
                            target_ptr->yMax() = script_ptr->yMax();
                            break;
                        case 2:
                            target_ptr->xMin() = script_ptr->xMin();
                            break;
                        case 3:
                            target_ptr->yMin() = script_ptr->yMin();
                            break;
                        default:
                            error(errInternal, -1, __FILE__ ":TextPageOCR::coalesce_superscript_and_subscript: Invalid rotation");
                    };
                }
                break;
            case RIGHT_SUPERSCRIPT:
                {
                    // DEBUG
                    //cerr << "RIGHT_SUPERSCRIPT (" << get<0>(*iter) << ") " << target_ptr->getText()->toStr() << "^" << script_ptr->getText()->toStr()
                    //    << "; target size = " << target_ptr->fontSize << "; script size = " << script_ptr->fontSize << "; count = " << count_iter->second.size() << endl;

                    for(string::const_iterator i = superscript_prefix.begin();i != superscript_prefix.end();++i){
                        target_ptr->chars.push_back( TextWordOCR::CharInfo( *i, *i, reference_char.edge, reference_char.font, true /*formatting metadata*/) );
                    }

                    for(std::deque<TextWordOCR::CharInfo>::const_iterator i = script_ptr->chars.begin();i != script_ptr->chars.end();++i){
                        target_ptr->chars.push_back( TextWordOCR::CharInfo( i->text, i->charcode, reference_char.edge, reference_char.font, false) );
                    }

                    for(string::const_iterator i = superscript_suffix.begin();i != superscript_suffix.end();++i){
                        target_ptr->chars.push_back( TextWordOCR::CharInfo( *i, *i, reference_char.edge, reference_char.font, true /*formatting metadata*/) );
                    }

                    // Include the script in the target bounding box
                    switch(target_ptr->rot){
                        case 0:
                            target_ptr->xMax() = script_ptr->xMax();
                            break;
                        case 1:
                            target_ptr->yMax() = script_ptr->yMax();
                            break;
                        case 2:
                            target_ptr->xMin() = script_ptr->xMin();
                            break;
                        case 3:
                            target_ptr->yMin() = script_ptr->yMin();
                            break;
                        default:
                            error(errInternal, -1, __FILE__ ":TextPageOCR::coalesce_superscript_and_subscript: Invalid rotation");
                    };
                }
                break;
            default:
                error(errInternal, -1, __FILE__ ":TextPageOCR::coalesce_superscript_and_subscript: Unknown script location");
        };
    }

    delete_invalid_words();
}

struct SortClustersInReadingOrder
{
    inline bool operator()(const WordCluster &m_a, const WordCluster &m_b) const
    {
        // Headers go first
        if( (m_a.getCategory() == WordCluster::HEADER) || (m_b.getCategory() == WordCluster::HEADER) ){
            if( (m_a.getCategory() == WordCluster::HEADER) && (m_b.getCategory() != WordCluster::HEADER) ){
                return true;
            }

            if( (m_a.getCategory() != WordCluster::HEADER) && (m_b.getCategory() == WordCluster::HEADER) ){
                return false;
            }
        }

        // Left margin text goes after headers
        if( (m_a.getCategory() == WordCluster::LEFT_MARGIN) || (m_b.getCategory() == WordCluster::LEFT_MARGIN) ){
            if( (m_a.getCategory() == WordCluster::LEFT_MARGIN) && (m_b.getCategory() != WordCluster::LEFT_MARGIN) ){
                return true;
            }

            if( (m_a.getCategory() != WordCluster::LEFT_MARGIN) && (m_b.getCategory() == WordCluster::LEFT_MARGIN) ){
                return false;
            }
        }

         // Right margin text goes after left margin text
        if( (m_a.getCategory() == WordCluster::RIGHT_MARGIN) || (m_b.getCategory() == WordCluster::RIGHT_MARGIN) ){
            if( (m_a.getCategory() == WordCluster::RIGHT_MARGIN) && (m_b.getCategory() != WordCluster::RIGHT_MARGIN) ){
                return true;
            }

            if( (m_a.getCategory() != WordCluster::RIGHT_MARGIN) && (m_b.getCategory() == WordCluster::RIGHT_MARGIN) ){
                return false;
            }
        }

        // Footers go last
        if( (m_a.getCategory() == WordCluster::FOOTER) || (m_b.getCategory() == WordCluster::FOOTER) ){
            if( (m_a.getCategory() == WordCluster::FOOTER) && (m_b.getCategory() != WordCluster::FOOTER) ){
                return false;
            }

            if( (m_a.getCategory() != WordCluster::FOOTER) && (m_b.getCategory() == WordCluster::FOOTER) ){
                return true;
            }
        }

        const PDFRectangle a = m_a.getBBox();
        const PDFRectangle b = m_b.getBBox();

        switch( m_a.getRotation() ){
            case 0:
                // Do the clusters overlap in X?
                if( ( a.xMin() > b.xMax() ) || ( b.xMin() > a.xMax() ) ){ // No overlap in X
                    return ( a.xMin() < b.xMin() );
                }
                else{ // Yes overlap in X
                    return ( a.yMin() < b.yMin() );
                }
                break;
            case 1:
                // Do the clusters overlap in Y?
                if( ( a.yMin() > b.yMax() ) || ( b.yMin() > a.yMax() ) ){ // No overlap in Y
                    return ( a.yMin() < b.yMin() );
                }
                else{ // Yes overlap in Y
                    return (a.xMax() > b.xMax());
                }
                break;
            case 2:
                // Do the clusters overlap in X?
                if( ( a.xMin() > b.xMax() ) || ( b.xMin() > a.xMax() ) ){ // No overlap in X
                    return ( a.xMax() > b.xMax() ) ;
                }
                else{ // Yes overlap in X
                    return ( a.yMax() > b.yMax() );
                }
                break;
            case 3:
                // Do the clusters overlap in Y?
                if( ( a.yMin() > b.yMax() ) || ( b.yMin() > a.yMax() ) ){ // No overlap in Y
                    return ( a.yMax() > b.yMax() );
                }
                else{ // Yes overlap in Y
                    return (a.xMin() < b.xMin());
                }
                break;
            default:
                error(errInternal, -1, __FILE__ ":SortClustersInReadingOrder: Invalid rotation");
        };

        return true; // Keep the compiler happy
    };
};

struct SortWordsInReadingOrder
{
    inline bool operator()(TextWordOCR* m_a, TextWordOCR* m_b) const
    {
        if(m_a->getBaseline() == m_b->getBaseline()){

            switch( m_a->getRotation() ){
                case 0:
                    return ( m_a->xMin() < m_b->xMin() );
                case 1:
                    return ( m_a->yMin() < m_b->yMin() );
                case 2:
                    return ( m_a->xMax() > m_b->xMax() );
                case 3:
                    return ( m_a->yMax() > m_b->yMax() );
                default:
                    error(errInternal, -1, __FILE__ ":SortWordsInReadingOrder: Invalid rotation");
            };

            return true; // Keep the compiler happy
        }

        switch( m_a->getRotation() ){
            case 0:
                return (m_a->getBaseline() < m_b->getBaseline());
            case 1:
                return (m_a->getBaseline() > m_b->getBaseline());
            case 2:
                return (m_a->getBaseline() > m_b->getBaseline());
            case 3:
                return (m_a->getBaseline() < m_b->getBaseline());
            default:
                error(errInternal, -1, __FILE__ ":SortWordsInReadingOrder: Invalid rotation");
        };

        return true; // Keep the compiler happy
    };
};

struct SortWordsValid
{
    inline bool operator()(TextWordOCR* m_a, TextWordOCR* m_b) const
    {
        // Sort valid words to the begining by defining m_a to compare less then m_b 
        // if m_a is valid and m_b is not valid
        return ( m_a->is_valid() && !m_b->is_valid() );
    };
};

struct SortClustersBySize
{
    inline bool operator()(const WordCluster &m_a, const WordCluster &m_b) const
    {
        return m_a.size() < m_b.size();
    };
};

void TextPageOCR::cluster_words()
{
    // Maximum allowed word spacing in each dimension as a fraction of the min(font size_i, font size_j)
    // between words i and j.
    const double primary_threshold = 1.0; // X dimension for rotation 0
    const double secondary_threshold = 1.4; // Y dimension for rotation 0

    // For now, words with different rotations do *not* interact
    for(std::list<TextWordOCR*>::iterator w = ptr_pool.begin();w != ptr_pool.end();++w){
        
        // Initially, each word defines a separate cluster
        clusters.push_back( WordCluster( *w ) ); // The cluster rotation is obtained from this initial word
    }

    const size_t init_num_cluster = clusters.size();

    // DEBUG
    //cerr << "Found " << init_num_cluster << " initial clusters (1 word/cluster)" << endl;

    SymmetricMatrix<double> d2(init_num_cluster);

    time_t profile = time(NULL);

    // Brute-force pairwise distance calculation
    for(size_t i = 0;i < init_num_cluster;++i){

        for(size_t j = i + 1;j < init_num_cluster;++j){
            if( clusters[i].getRotation() == clusters[j].getRotation() ){
                d2(i, j) = clusters[i].distance2(clusters[j], primary_threshold, secondary_threshold);
            }
        }
    }

    while(true){ // Iteratively merge clusters using single linkage clustering

        const size_t num_clusters = clusters.size();
        
        double best_distance2 = std::numeric_limits<double>::max();
        size_t best_i = std::numeric_limits<size_t>::max();
        size_t best_j = std::numeric_limits<size_t>::max();

        // Brute-force pairwise distance calculation (optimize later)
        for(size_t i = 0;i < num_clusters;++i){

            for(size_t j = i + 1;j < num_clusters;++j){

                if( ( clusters[i].getRotation() == clusters[j].getRotation() ) && (d2(i, j) < best_distance2) ){

                    best_distance2 = d2(i, j);
                    best_i = i;
                    best_j = j;
                }
            }
        }

        if( max(best_i, best_j) == std::numeric_limits<size_t>::max()){
            break; // We did not find a pair of clusters to merge
        }

        // Merge best_j into best_i
        clusters[best_i].insert( clusters[best_i].end(), clusters[best_j].begin(), clusters[best_j].end() );
        clusters[best_j].clear();

        // Update the distance matrix to account for the contents of cluster[best_j] being added to the cluster[best_i]
        for(size_t k = 0;k < num_clusters;++k){
            d2(best_i, k) = min( d2(best_i, k), d2(best_j, k) );
        }

        if( best_j != (num_clusters - 1) ){

            swap(clusters[best_j], clusters[num_clusters - 1]);

            // Update the distance matrix to account for swapping cluster[best_j] with cluster[num_clusters - 1]
            for(size_t k = 0;k < num_clusters;++k){
                d2(best_j, k) = d2(num_clusters - 1, k);
            }
        }

        clusters.pop_back();
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    // Look for header, left margin, right margin, and footer clusters
    const double header_max_threshold = 0.08*pageHeight; // <-- Heuristic thresholds should be #defined
    const double left_margin_max_threshold = 0.06*pageWidth;
    const double footer_min_threshold = 0.90*pageHeight;
    const double right_margin_min_threshold = 0.95*pageWidth;
    
    // DEBUG
    //cerr << "pageHeight = " << pageHeight << endl;
    //cerr << "pageWidth = " << pageWidth << endl;
    //cerr << "header_max_threshold = " << header_max_threshold << endl;
    //cerr << "left_margin_max_threshold = " << left_margin_max_threshold << endl;
    //cerr << "footer_min_threshold = " << footer_min_threshold << endl;
    //cerr << "right_margin_min_threshold = " << right_margin_min_threshold << endl;

    deque<size_t> header_cluster_indicies;
    deque<size_t> left_margin_cluster_indicies;
    deque<size_t> footer_cluster_indicies;
    deque<size_t> right_margin_cluster_indicies;

    for(size_t i = 0;i < clusters.size();++i){
        
        // Header and foot clusters are required to have less than three lines of text
        // (this is intended to protect against "run-away" clustering that merges header information
        // with primary text)
        //if(clusters[i].num_line() > 3){
        //    continue;
        //}

        const PDFRectangle bbox = clusters[i].getBBox();

        if(bbox.yMax() <= header_max_threshold){ // The entire cluster must be above (less than) the header threshold
            header_cluster_indicies.push_back(i);
        }

        if(bbox.xMax() <= left_margin_max_threshold){ // The entire cluster must be to the left of (less than) than the left margin threshold
            left_margin_cluster_indicies.push_back(i);
        }

        if(bbox.yMin() >= footer_min_threshold){ // The entire cluster must be below (greater than) the footer threshold 
            footer_cluster_indicies.push_back(i);
        }

        if(bbox.xMin() >= right_margin_min_threshold){ // The entire cluster must be to the right of (greater than) than the right margin threshold
            right_margin_cluster_indicies.push_back(i);
        }
    }

    //cerr << "Found " << header_cluster_indicies.size() << " header clusters" << endl;
    //cerr << "Found " << left_margin_cluster_indicies.size() << " left-margin clusters" << endl;
    //cerr << "Found " << footer_cluster_indicies.size() << " footer clusters" << endl;
    //cerr << "Found " << right_margin_cluster_indicies.size() << " right-margin clusters" << endl;

    // Label and merge HEADER clusters
    for(deque<size_t>::const_iterator i = header_cluster_indicies.begin();i != header_cluster_indicies.end();++i){
        
        clusters[*i].setCategory(WordCluster::HEADER); // Label the cluster

        // If we have multiple clusters, merge all words into the first cluster
        if( i != header_cluster_indicies.begin() ){
            clusters[ header_cluster_indicies.front() ].insert( clusters[ header_cluster_indicies.front() ].end(), clusters[*i].begin(), clusters[*i].end() );
            clusters[*i].clear();
        }
    }

    // Label and merge LEFT_MARGIN clusters
    for(deque<size_t>::const_iterator i = left_margin_cluster_indicies.begin();i != left_margin_cluster_indicies.end();++i){
        
        clusters[*i].setCategory(WordCluster::LEFT_MARGIN); // Label the cluster

        // If we have multiple clusters, merge all words into the first cluster
        if( i != left_margin_cluster_indicies.begin() ){

            clusters[ left_margin_cluster_indicies.front() ].insert( clusters[ left_margin_cluster_indicies.front() ].end(), clusters[*i].begin(), clusters[*i].end() );
            clusters[*i].clear();
        }
    }

    // Label and merge FOOTER clusters
    for(deque<size_t>::const_iterator i = footer_cluster_indicies.begin();i != footer_cluster_indicies.end();++i){
        
        clusters[*i].setCategory(WordCluster::FOOTER); // Label the cluster

        // If we have multiple clusters, merge all words into the first cluster
        if( i != footer_cluster_indicies.begin() ){
            clusters[ footer_cluster_indicies.front() ].insert(clusters[ footer_cluster_indicies.front() ].end(), clusters[*i].begin(), clusters[*i].end());
                clusters[*i].clear();
        }
    }

    // Label and merge RIGHT_MARGIN clusters
    for(deque<size_t>::const_iterator i = right_margin_cluster_indicies.begin();i != right_margin_cluster_indicies.end();++i){
        
        clusters[*i].setCategory(WordCluster::RIGHT_MARGIN); // Label the cluster

        // If we have multiple clusters, merge all words into the first cluster
        if( i != right_margin_cluster_indicies.begin() ){

            clusters[ right_margin_cluster_indicies.front() ].insert( clusters[ right_margin_cluster_indicies.front() ].end(), clusters[*i].begin(), clusters[*i].end() );
            clusters[*i].clear();
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    // Look for REFERENCE clusters
    // References in scientific publications tend to be lists of publications
    // with left justified starting lines (with either a numeric digit or capital letter as the starting character)
    // followed by zero or more intented lines (containing the reference metadata).
    
    ////////////////////////////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    // Look for TEXT clusters
    // "Text" in this context refers to the narrative content of a scientific publication
    for(deque<WordCluster>::iterator i = clusters.begin();i != clusters.end();++i){

        if(i->getCategory() != WordCluster::UNKNOWN){
            continue; // Skip clusters that have already be labeled
        }

        const size_t num_lines = i->num_line();
        const size_t num_words = i->count_cluster_words();
        const double words_per_line = double(num_words)/num_lines;

        // Hand-carved decision tree! These thresholds need to be fine-tuned (and undoubtably supplemented with additional metrics)
        if(num_lines > 1){

            if(i->line_multiword_density() > 0.7){

                if(words_per_line > 5.0){
                    i->setCategory(WordCluster::TEXT);
                }
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    // Attempt to infer DATA clusters (i.e., tables or figures) by iteratively clustering "UNKNOWN" 
    // clusters while ensuring that the rectangular bounding box of the resulting cluster does not
    // overlap an existing cluster that is *not* labeled as "UNKNOWN".
    // This strategy effectively assumes that all clusters that are not currently labled are DATA clusters

    // DEBUG
    //cerr << "Inferring DATA clusters" << endl;

    while(true){

        const size_t num_clusters = clusters.size();

        // The separation distances between UNKNOWN clusters
        deque< tuple<double /*distance^2*/, size_t /*index i*/, size_t /*index j*/> > cluster_separation;

        // Brute-force pairwise distance calculation (optimize later)
        for(size_t i = 0;i < num_clusters;++i){

            if(clusters[i].getCategory() != WordCluster::UNKNOWN){
                continue;
            }

            for(size_t j = i + 1;j < num_clusters;++j){

                if( (clusters[j].getCategory() != WordCluster::UNKNOWN) || 
                    ( clusters[i].getRotation() != clusters[j].getRotation() ) ){
                    continue;
                }

                const double local_d2 = clusters[i].distance2(clusters[j], primary_threshold, secondary_threshold);

                cluster_separation.push_back( make_tuple(local_d2, i, j) );
            }
        }

        // Sort cluster pairs in order of ascending cluster separation
        sort( cluster_separation.begin(), cluster_separation.end() );

        while( !cluster_separation.empty() ){

            const size_t trial_index_i = get<1>( cluster_separation.front() );
            const size_t trial_index_j = get<2>( cluster_separation.front() );

            cluster_separation.pop_front();

            WordCluster trial(clusters[trial_index_i]);

            trial.insert(trial.end(), clusters[trial_index_j].begin(), clusters[trial_index_j].end());

            const PDFRectangle trial_bbox = trial.getBBox();

            // Does this trial cluster overlap any previously annotated clusters?
            bool has_overlap = false;

            for(deque<WordCluster>::const_iterator i = clusters.begin();i != clusters.end();++i){
                if(i->getCategory() == WordCluster::UNKNOWN){
                    continue;
                }

                const PDFRectangle bbox = i->getBBox();

                // Test for bounding box overlap
                if( ( bbox.xMin() > trial_bbox.xMax() ) || ( trial_bbox.xMin() > bbox.xMax() ) ||
                    ( bbox.yMin() > trial_bbox.yMax() ) || ( trial_bbox.yMin() > bbox.yMax() ) ){
                    continue;
                }

                has_overlap = true;
            }

            if(has_overlap){
                continue;
            }

            // We found a valid pair of UNKNOWN clusters to merge
            clusters[trial_index_i] = trial;

            if( trial_index_j != (num_clusters - 1) ){
                swap(clusters[trial_index_j], clusters[num_clusters - 1]);
            }

            clusters.pop_back();

            break;
        }

        if( num_clusters == clusters.size() ){
            break; // We were unable to merge any UNKNOWN clusters
        }
    }

    // DEBUG
    //cerr << "Finished inferring DATA clusters" << endl;

    for(deque<WordCluster>::iterator i = clusters.begin();i != clusters.end();++i){

        // Convert all UNKNOWN clusters into DATA clusters
        if(i->getCategory() == WordCluster::UNKNOWN){
            if(i->num_line() == 1){
                // A single line of text is likely a section header or orphan text line
                i->setCategory(WordCluster::TEXT);
            }
            else{
                i->setCategory(WordCluster::DATA);
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////

    // Sort clusters by cluster size to remove empty clusters
    sort( clusters.begin(), clusters.end(), SortClustersBySize() );

    while( !clusters.empty() && clusters.front().empty() ){
        clusters.pop_front();
    }

    // DEBUG
    //cerr << "Found " << clusters.size() << " final clusters" << endl;

    for(deque<WordCluster>::iterator i = clusters.begin();i != clusters.end();++i){
        
        // Adjust the baseline values for words within individual clusters
        i->align_baselines();

        // Combine adjacent word fragments that share a baseline (and aren't separated by a space)
        i->merge_word_fragments();
    }

    // Since merge_word_fragments() can mark words for deletion and remove them from clusters, we need to free the memory from the underlying word list 
    delete_invalid_words();

    // Sort the clusters in reading order
    sort( clusters.begin(), clusters.end(), SortClustersInReadingOrder() );

    // Sort the words within each cluster
    for(deque<WordCluster>::iterator i = clusters.begin();i != clusters.end();++i){
        sort( i->begin(), i->end(), SortWordsInReadingOrder() );
    }

    //#define DISPLAY_CLUSTERS
    #ifdef DISPLAY_CLUSTERS
    // Display the clusters
    for(deque<WordCluster>::const_iterator i = clusters.begin();i != clusters.end();++i){

        cout << "***************** cluster #" << (i - clusters.begin()) << " [";
        
        switch(i->getCategory()){
            case WordCluster::TEXT:
                cout << "TEXT";
                break;
            case WordCluster::DATA:
                cout << "DATA";
                break;
            case WordCluster::REFERENCE:
                cout << "REFERENCE";
                break;
            case WordCluster::HEADER:
                cout << "HEADER";
                break;
            case WordCluster::LEFT_MARGIN:
                cout << "LEFT_MARGIN";
                break;
            case WordCluster::RIGHT_MARGIN:
                cout << "RIGHT_MARGIN";
                break;
            case WordCluster::FOOTER:
                cout << "FOOTER";
                break;
            case WordCluster::UNKNOWN:
                cout << "UNKNOWN";
                break;
            default:
                error(errInternal, -1, __FILE__ ": Unknown cluster label!!");
        };

        cout << "] *****************" << endl;

        cout << "area = " << i->area() << endl;
        cout << "word_area = " << i->word_area() << endl;
        cout << "word_density = " << i->word_area()/i->area() << endl;
        cout << "line_multiword_density = " << i->line_multiword_density() << endl;
        const size_t num_cluster_words = i->count_cluster_words(); 
        cout << "number of words = " << num_cluster_words << endl;
        cout << "number of lines = " << i->num_line() << endl;
        cout << "<words/line> = " << double( num_cluster_words )/i->num_line() << endl;

        const PDFRectangle cluster_bbox = i->getBBox();

        cout << "width = " << ( cluster_bbox.xMax() - cluster_bbox.xMin() ) << endl;
        cout << "height = " << ( cluster_bbox.yMax() - cluster_bbox.yMin() ) << endl;

        for(WordCluster::const_iterator j = i->begin();j != i->end();++j){
            cout << "\t\"";
            
            for(size_t k = 0;k < (*j)->getLength();++k){
                cout << char( (* (*j)->getChar(k) ) & 0xFF);
            }

            cout << "\" -> x:" << (*j)->xMin() << ".." << (*j)->xMax() << "; y:" << (*j)->yMin() << ".." << (*j)->yMax() << "; baseline = " << (*j)->getBaseline() 
                << "; font size = " << (*j)->getFontSize() << ";rot = " << (*j)->getRotation() << endl;
        }
    }
    #endif // DISPLAY_CLUSTERS
}

deque< pair<double, int> >::const_iterator find_closest_col(const deque< pair<double, int> >::const_iterator &m_begin, 
    const deque< pair<double, int> >::const_iterator &m_end, const double &m_query, const double &m_threshold)
{
    deque< pair<double, int> >::const_iterator ret = m_end;
    double best_match = std::numeric_limits<double>::max();

    for(deque< pair<double, int> >::const_iterator i = m_begin;i != m_end;++i){

        const double delta = fabs(i->first - m_query);

        if(delta < best_match){

            best_match = delta;
            ret = i;
        }
    }

    return (best_match <= m_threshold) ? ret : m_end;
}

int interpolate_col(const deque< pair<double, int> >::const_iterator &m_begin, 
    const deque< pair<double, int> >::const_iterator &m_end, const double &m_query)
{
    deque< pair<double, int> >::const_iterator lower = m_end;
    double best_lower = std::numeric_limits<double>::max();

    deque< pair<double, int> >::const_iterator upper = m_end;
    double best_upper = std::numeric_limits<double>::lowest();

    for(deque< pair<double, int> >::const_iterator i = m_begin;i != m_end;++i){

        const double delta = m_query - i->first;

        if(delta == 0.0){
            return i->second;
        }

        if( (delta > 0) && (delta < best_lower) ){

            best_lower = delta;
            lower = i;
        }

        if( (delta < 0) && (delta > best_upper) ){
            
            best_upper = delta;
            upper = i;
        }
    }

    if( (upper == m_end) || (lower == m_end) ){
        return -1;
    }

    // Use linear interpolation to estimate the column
    return lower->second + (m_query - lower->first)*(upper->second - lower->second)/(upper->first - lower->first);
}

void TextPageOCR::dump(void *outputStream, TextOutputFunc outputFunc, EndOfLineKind textEOL, bool pageBreaks,
    bool m_display_header, bool m_display_left_margin, bool m_display_right_margin, bool m_display_footer)
{
    const UnicodeMap *uMap;
    char space[8], eol[16], eop[8];
    int spaceLen, eolLen, eopLen;

    // get the output encoding
    if (!(uMap = globalParams->getTextEncoding())) {
        return;
    }
    spaceLen = uMap->mapUnicode(0x20, space, sizeof(space));
    eolLen = 0; // make gcc happy
    switch (textEOL) {
    case eolUnix:
        eolLen = uMap->mapUnicode(0x0a, eol, sizeof(eol));
        break;
    case eolDOS:
        eolLen = uMap->mapUnicode(0x0d, eol, sizeof(eol));
        eolLen += uMap->mapUnicode(0x0a, eol + eolLen, sizeof(eol) - eolLen);
        break;
    case eolMac:
        eolLen = uMap->mapUnicode(0x0d, eol, sizeof(eol));
        break;
    }

    eopLen = uMap->mapUnicode(0x0c, eop, sizeof(eop));
        
    bool first_cluster = true;

    // The coalesce() function is responsible for sorting the word clusters into the correct
    // display order
    for(std::deque<WordCluster>::iterator i = clusters.begin();i != clusters.end();++i){

        if( i->empty() ){
            cerr << "Warning! Empty word cluster!" << endl;
            continue;
        }

        // Undo any text rotation to simpify the formatting logic
        for(WordCluster::iterator j = i->begin();j != i->end();++j){
            (*j)->undo_rotation(); // Undo any TextWordOCR rotation. This leads to dump() modifying the underlying PDF data ...
        }

        // Separate clusters by new lines
        if(first_cluster == false){
            (*outputFunc)(outputStream, eol, eolLen);
        }

        switch( i->getCategory() ){

            case WordCluster::TEXT:
            case WordCluster::REFERENCE:
                {
                    GooString s;
                    std::vector<Unicode> uText;
                    double prev_baseline = i->front()->getBaseline();
                    PDFRectangle prev_bbox;
                    
                    for(WordCluster::const_iterator j = i->begin();j != i->end();++j){

                        if( (*j)->getBaseline() != prev_baseline ){

                            prev_baseline = (*j)->getBaseline();
                            (*outputFunc)(outputStream, eol, eolLen);
                        }
                        else{
                            if( ( (*j)->xMin() - prev_bbox.xMax() ) > minWordSpacing * (*j)->fontSize ){
                                (*outputFunc)(outputStream, space, spaceLen);
                            }
                        }

                        s.clear();
                        uText.resize( (*j)->getLength() );
                        std::ranges::transform( (*j)->chars, uText.begin(), [](auto &c) { return c.text; } );
                        dumpFragment(uText.data(), uText.size(), uMap, &s);
                        (*outputFunc)(outputStream, s.c_str(), s.getLength());
                        
                        prev_bbox = *(*j);
                    }

                    first_cluster = false;
                }
                break;
            case WordCluster::DATA:
            case WordCluster::UNKNOWN:
                {
                    // Metadata
                    //const string meta_data_begin = "<data>";
                    //const string meta_data_end = "</data>";

                    //(*outputFunc)( outputStream, meta_data_begin.c_str(), meta_data_begin.size() );
                    //(*outputFunc)(outputStream, eol, eolLen);

                    // Write data clusters using a physical layout relative the bounding box of the cluster
                    const PDFRectangle cluster_bbox = i->getBBox();

                    double starting_edge = cluster_bbox.xMin(); // Justify text from the left edge

                    // DEBUG
                    //cerr << "cluster_bbox bbox: x1=" << cluster_bbox.x1 << "; y1=" << cluster_bbox.y1 << "; x2=" << cluster_bbox.x2 << "; y2=" << cluster_bbox.y2 << "" << endl;

                    // Store the coordinate-to-column mapping to maintain left-right table justification
                    deque< pair<double, int> > coord_to_col;

                    int col = 0;

                    GooString s;
                    std::vector<Unicode> uText;
                    double prev_baseline = i->front()->getBaseline();

                    WordCluster::iterator j = i->begin();

                    while( j != i->end() ){

                        // Collect the words that are seperated by a single space
                        WordCluster::iterator first_word = j;
                        WordCluster::iterator last_word = first_word;
                        WordCluster::iterator next_word = last_word + 1;

                        const PDFRectangle first_bbox = *(*j);
                        PDFRectangle last_bbox = first_bbox;
                        
                        bool first_word_on_line = false;

                        if( (*j)->getBaseline() != prev_baseline ){

                            prev_baseline = (*j)->getBaseline();
                            starting_edge = cluster_bbox.xMin(); // Justify text from the left edge
                            (*outputFunc)(outputStream, eol, eolLen);
                            col = 0;
                            first_word_on_line = true;
                        }

                        // The number of columns of text that will be needed to store the words [first_word, last_word]
                        int first_to_last_col = (*first_word)->getLength();
                        int real_first_to_last_col = (*first_word)->getLength(true); // Excluding formatting characters

                        while( next_word != i->end() ){

                            if( (*last_word)->getBaseline() != (*next_word)->getBaseline() ){
                                (*last_word)->setSpaceAfter(false);
                                break;
                            }

                            const double min_font_size = std::min( (*last_word)->fontSize, (*next_word)->fontSize );

                            if( ( (*next_word)->xMin() - last_bbox.xMax() ) <= maxWordSpacing * min_font_size ){
                                
                                if( ( (*next_word)->xMin() - last_bbox.xMax() ) > minWordSpacing * min_font_size ){
                                    ++first_to_last_col; // a space will occupy a column
                                    ++real_first_to_last_col;
                                    (*last_word)->setSpaceAfter(true);
                                }
                                else{
                                    (*last_word)->setSpaceAfter(false);
                                }

                                last_word = next_word;
                                last_bbox = *(*next_word);

                                first_to_last_col += (*last_word)->getLength();
                                real_first_to_last_col += (*last_word)->getLength(true); // Excluding formatting characters

                                ++next_word;
                            }
                            else{
                                (*last_word)->setSpaceAfter(false);
                                break;
                            }
                        }
                        
                        j = next_word; // For the next iteration

                        // The contiguous (or single space separated) words are now stored in [first_word, last_word]

                        int num_space_added = 0;

                        // DEBUG
                        //for(size_t k = 0;k < (*first_word)->getLength();++k){
                        //    cerr << char( (* (*first_word)->getChar(k) ) & 0xFF);
                        //}

                        //cerr << " (baseline=" << (*first_word)->getBaseline() << ")";

                        // Should we left justify the words?
                        //unordered_map<double, int>::const_iterator iter = coord_to_col.find( round(first_xMin) );
                        deque< pair<double, int> >::const_iterator iter = 
                            find_closest_col(coord_to_col.begin(), coord_to_col.end(), first_bbox.xMin(), maxWordSpacing * (*first_word)->fontSize);

                        if( iter != coord_to_col.end() ){ // Left justify

                            // DEBUG
                            //cerr << "\tLeft justify to " << iter->second << endl;

                            while(col < iter->second){ // Pad with spaces

                                (*outputFunc)(outputStream, space, spaceLen);
                                ++col;
                                ++num_space_added;
                            }
                        }
                        else{

                            // Should we right justify the words?
                            iter = find_closest_col(coord_to_col.begin(), coord_to_col.end(), last_bbox.xMax(), maxWordSpacing * (*last_word)->fontSize);

                            if(iter != coord_to_col.end() ){ // Right justify

                                // DEBUG
                                //cerr << "\tRight justify to " << iter->second << endl;

                                while( (col + first_to_last_col) < iter->second ){ // Pad with spaces

                                    (*outputFunc)(outputStream, space, spaceLen);
                                    ++col;
                                    ++num_space_added;
                                }
                            }
                            else{
                                
                                // Should we center justify the words?
                                iter = find_closest_col(coord_to_col.begin(), coord_to_col.end(), 0.5*( last_bbox.xMax() + last_bbox.xMin() ), maxWordSpacing * (*last_word)->fontSize);

                                if(iter != coord_to_col.end() ){ // Center justify

                                    // DEBUG
                                    //cerr << "\t***Center justify to " << iter->second << endl;

                                    while( (col + first_to_last_col/2) < iter->second ){ // Pad with spaces

                                        (*outputFunc)(outputStream, space, spaceLen);
                                        ++col;
                                        ++num_space_added;
                                    }
                                }
                                else{
                                    // Approximate spacing
                                    int approx_col = interpolate_col( coord_to_col.begin(), coord_to_col.end(), first_bbox.xMin() );

                                    // DEBUG
                                    //cerr << "\tApprox justify to " << approx_col << endl;

                                    if(approx_col >= 0){ // Approximate left justification

                                        while(col < approx_col){ // Pad with spaces

                                            (*outputFunc)(outputStream, space, spaceLen);
                                            ++col;
                                            ++num_space_added;
                                        }
                                    }
                                    else{

                                        approx_col = interpolate_col( coord_to_col.begin(), coord_to_col.end(), last_bbox.xMax() );

                                        // DEBUG
                                        //cerr << "\tInterpolate justify to " << approx_col << endl;

                                        if(approx_col >= 0){ // Approximate right justification

                                            while( (col + first_to_last_col) < approx_col ){ // Pad with spaces

                                                (*outputFunc)(outputStream, space, spaceLen);
                                                ++col;
                                                ++num_space_added;
                                            }
                                        }
                                        else{
                                            const double delta_space = first_bbox.xMin() - starting_edge;

                                            // To get an accurate estimate of the average character width, we need to exclude added formatting metadata characters
                                            const double ave_char_width = real_first_to_last_col ? ( last_bbox.xMax() - first_bbox.xMin() )/real_first_to_last_col : 0.0;

                                            const int num_space = (ave_char_width > 0.0) ? round(delta_space/ave_char_width) : 0;

                                            // DEBUG
                                            //cerr << "\tRaw space " << num_space << ";first_to_last_col = " << first_to_last_col << ";real_first_to_last_col = " << real_first_to_last_col 
                                            //    << ";ave_char_width = " << ave_char_width << ";delta space = " << delta_space << endl;
                                            //cerr << "\tfirst_bbox.xMin() = " << first_bbox.xMin() << "; starting_edge = " << starting_edge << ";last_bbox.xMax() = " << last_bbox.xMax() << endl;

                                            for(int k = 0;k < num_space;++k){

                                                (*outputFunc)(outputStream, space, spaceLen);
                                                ++col;
                                                ++num_space_added;
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Make sure we add at least one space between distinct word groups on the same line
                        if( !first_word_on_line && (num_space_added == 0) ){

                            (*outputFunc)(outputStream, space, spaceLen);
                            ++col;
                        }

                        coord_to_col.push_back( make_pair(first_bbox.xMin(), col) ); // position of first character

                        int begin_col = col;

                        for(WordCluster::iterator k = first_word;k != (last_word + 1);++k){

                            s.clear();
                            uText.resize( (*k)->getLength() );
                            std::ranges::transform( (*k)->chars, uText.begin(), [](auto &c) { return c.text; } );
                            dumpFragment(uText.data(), uText.size(), uMap, &s);
                            (*outputFunc)(outputStream, s.c_str(), s.getLength());

                            col += (*k)->getLength();

                            if( (*k)->getSpaceAfter() ){

                                (*outputFunc)(outputStream, space, spaceLen);
                                ++col;
                            }
                        }

                        starting_edge = last_bbox.xMax();

                        if( (col - begin_col) > 2){
                            
                            coord_to_col.push_back( make_pair( 0.5*( first_bbox.xMin() + last_bbox.xMax() ), (col - 1 + begin_col)/2) ); // position of center character
                            //cerr << "\tmid X (" << 0.5*(first_xMin + last_xMax) << ") mapped to column " << (col - 1 + begin_col)/2 << endl;

                            coord_to_col.push_back( make_pair(last_bbox.xMax(), col - 1) ); // position of last character
                            //cerr << "\tlast xMin (" << last_xMax << ") mapped to column " << col - 1 << endl;
                        }
                    }

                    (*outputFunc)(outputStream, eol, eolLen);
                    //(*outputFunc)( outputStream, meta_data_end.c_str(), meta_data_end.size() );

                    first_cluster = false;
                }
                break;
            case WordCluster::HEADER:
                if(m_display_header){

                    // Metadata
                    //const string meta_data_begin = "<header>"; // DEBUG
                    //const string meta_data_end = "</header>";
                    //(*outputFunc)( outputStream, meta_data_begin.c_str(), meta_data_begin.size() );
                    //(*outputFunc)(outputStream, eol, eolLen);

                    GooString s;
                    std::vector<Unicode> uText;
                    bool needs_space = false;
                    double prev_baseline = i->front()->getBaseline();
                    PDFRectangle prev_bbox;
                    
                    for(WordCluster::const_iterator j = i->begin();j != i->end();++j){

                        if( (*j)->getBaseline() != prev_baseline ){

                            prev_baseline = (*j)->getBaseline();
                            needs_space = false;
                            (*outputFunc)(outputStream, eol, eolLen);
                        }

                        if(needs_space && ( ( (*j)->xMin() - prev_bbox.xMax() ) > minWordSpacing * (*j)->fontSize) ){
                            (*outputFunc)(outputStream, space, spaceLen);
                        }

                        s.clear();
                        uText.resize( (*j)->getLength() );
                        std::ranges::transform( (*j)->chars, uText.begin(), [](auto &c) { return c.text; } );
                        dumpFragment(uText.data(), uText.size(), uMap, &s);
                        (*outputFunc)(outputStream, s.c_str(), s.getLength());
                        needs_space = true;
                        
                        prev_bbox = *(*j);
                    }

                    first_cluster = false;

                    (*outputFunc)(outputStream, eol, eolLen);
                    //(*outputFunc)( outputStream, meta_data_end.c_str(), meta_data_end.size() );
                }
                break;
            case WordCluster::LEFT_MARGIN:
                if(m_display_left_margin){

                    // Metadata
                    //const string meta_data_begin = "<left_margin>"; // DEBUG
                    //const string meta_data_end = "</left_margin>";
                    //(*outputFunc)( outputStream, meta_data_begin.c_str(), meta_data_begin.size() );
                    //(*outputFunc)(outputStream, eol, eolLen);

                    GooString s;
                    std::vector<Unicode> uText;
                    bool needs_space = false;
                    double prev_baseline = i->front()->getBaseline();
                    PDFRectangle prev_bbox;
                    
                    for(WordCluster::const_iterator j = i->begin();j != i->end();++j){

                        if( (*j)->getBaseline() != prev_baseline ){

                            prev_baseline = (*j)->getBaseline();
                            needs_space = false;
                            (*outputFunc)(outputStream, eol, eolLen);
                        }

                        if(needs_space && ( ( (*j)->xMin() - prev_bbox.xMax() ) > minWordSpacing * (*j)->fontSize) ){
                            (*outputFunc)(outputStream, space, spaceLen);
                        }

                        s.clear();
                        uText.resize( (*j)->getLength() );
                        std::ranges::transform( (*j)->chars, uText.begin(), [](auto &c) { return c.text; } );
                        dumpFragment(uText.data(), uText.size(), uMap, &s);
                        (*outputFunc)(outputStream, s.c_str(), s.getLength());
                        needs_space = true;
                        
                        prev_bbox = *(*j);
                    }

                    first_cluster = false;

                    (*outputFunc)(outputStream, eol, eolLen);
                    //(*outputFunc)( outputStream, meta_data_end.c_str(), meta_data_end.size() );
                }
                break;
            case WordCluster::RIGHT_MARGIN:
                if(m_display_right_margin){

                    // Metadata
                    //const string meta_data_begin = "<right_margin>"; // DEBUG
                    //const string meta_data_end = "</right_margin>";
                    //(*outputFunc)( outputStream, meta_data_begin.c_str(), meta_data_begin.size() );
                    //(*outputFunc)(outputStream, eol, eolLen);

                    GooString s;
                    std::vector<Unicode> uText;
                    bool needs_space = false;
                    double prev_baseline = i->front()->getBaseline();
                    PDFRectangle prev_bbox;
                    
                    for(WordCluster::const_iterator j = i->begin();j != i->end();++j){

                        if( (*j)->getBaseline() != prev_baseline ){

                            prev_baseline = (*j)->getBaseline();
                            needs_space = false;
                            (*outputFunc)(outputStream, eol, eolLen);
                        }

                        if(needs_space && ( ( (*j)->xMin() - prev_bbox.xMax() ) > minWordSpacing * (*j)->fontSize) ){
                            (*outputFunc)(outputStream, space, spaceLen);
                        }

                        s.clear();
                        uText.resize( (*j)->getLength() );
                        std::ranges::transform( (*j)->chars, uText.begin(), [](auto &c) { return c.text; } );
                        dumpFragment(uText.data(), uText.size(), uMap, &s);
                        (*outputFunc)(outputStream, s.c_str(), s.getLength());
                        needs_space = true;
                        
                        prev_bbox = *(*j);
                    }

                    first_cluster = false;

                    (*outputFunc)(outputStream, eol, eolLen);
                    //(*outputFunc)( outputStream, meta_data_end.c_str(), meta_data_end.size() );
                }
                break;
            case WordCluster::FOOTER:
                if(m_display_footer){

                    // Metadata
                    //const string meta_data_begin = "<footer>"; // DEBUG
                    //const string meta_data_end = "</footer>";
                    //(*outputFunc)( outputStream, meta_data_begin.c_str(), meta_data_begin.size() );
                    //(*outputFunc)(outputStream, eol, eolLen);

                    GooString s;
                    std::vector<Unicode> uText;
                    bool needs_space = false;
                    double prev_baseline = i->front()->getBaseline();
                    PDFRectangle prev_bbox;
                    
                    for(WordCluster::const_iterator j = i->begin();j != i->end();++j){

                        if( (*j)->getBaseline() != prev_baseline ){

                            prev_baseline = (*j)->getBaseline();
                            needs_space = false;
                            (*outputFunc)(outputStream, eol, eolLen);
                        }

                        if(needs_space && ( ( (*j)->xMin() - prev_bbox.xMax() ) > minWordSpacing * (*j)->fontSize) ){
                            (*outputFunc)(outputStream, space, spaceLen);
                        }

                        s.clear();
                        uText.resize( (*j)->getLength() );
                        std::ranges::transform( (*j)->chars, uText.begin(), [](auto &c) { return c.text; } );
                        dumpFragment(uText.data(), uText.size(), uMap, &s);
                        (*outputFunc)(outputStream, s.c_str(), s.getLength());
                        needs_space = true;
                        
                        prev_bbox = *(*j);
                    }

                    first_cluster = false;

                    (*outputFunc)(outputStream, eol, eolLen);
                    //(*outputFunc)( outputStream, meta_data_end.c_str(), meta_data_end.size() );
                }
                break;
            default:
                error(errInternal, -1, __FILE__ ":TextPageOCR::dump: Unknown cluster category");
        };
    }

    // end of page
    if (pageBreaks) {
        (*outputFunc)(outputStream, eop, eopLen);
    }
}

void TextPageOCR::setMergeCombining(bool merge)
{
    mergeCombining = merge;
}

int TextPageOCR::dumpFragment(const Unicode *text, int len, const UnicodeMap *uMap, GooString *s) const
{
    if (uMap->isUnicode()) {
        return reorderText(text, len, uMap, primaryLR, s, nullptr);
    } else {
        int nCols = 0;

        char buf[8];
        int buflen = 0;

        for (int i = 0; i < len; ++i) {
            buflen = uMap->mapUnicode(text[i], buf, sizeof(buf));
            s->append(buf, buflen);
            nCols += buflen;
        }

        return nCols;
    }
}

//------------------------------------------------------------------------
// ActualTextOCR
//------------------------------------------------------------------------
ActualTextOCR::ActualTextOCR(TextPageOCR *out)
{
    out->incRefCnt();
    text = out;
    actualText = nullptr;
    actualTextNBytes = 0;
}

ActualTextOCR::~ActualTextOCR()
{
    text->decRefCnt();
}

void ActualTextOCR::addChar(const GfxState *state, double x, double y, double dx, double dy, CharCode c, int nBytes, const Unicode *u, int uLen)
{
    if (!actualText) {
        text->addChar(state, x, y, dx, dy, c, nBytes, u, uLen);
        return;
    }

    // Inside ActualTextOCR span.
    if (!actualTextNBytes) {
        actualTextX0 = x;
        actualTextY0 = y;
    }
    actualTextX1 = x + dx;
    actualTextY1 = y + dy;
    actualTextNBytes += nBytes;
}

void ActualTextOCR::begin(const GfxState *state, const GooString *t)
{
    actualText = t->copy();
    actualTextNBytes = 0;
}

void ActualTextOCR::end(const GfxState *state)
{
    // ActualTextOCR span closed. Output the span text and the
    // extents of all the glyphs inside the span

    if (actualTextNBytes) {
        // now that we have the position info for all of the text inside
        // the marked content span, we feed the "ActualTextOCR" back through
        // text->addChar()
        std::vector<Unicode> uni = TextStringToUCS4(actualText->toStr());
        text->addChar(state, actualTextX0, actualTextY0, actualTextX1 - actualTextX0, actualTextY1 - actualTextY0, 0, actualTextNBytes, uni.data(), uni.size());
    }

    actualText.reset();
    actualTextNBytes = 0;
}

//------------------------------------------------------------------------
// TextOutputDev
//------------------------------------------------------------------------

static void TextOutputDev_outputToFile(void *stream, const char *text, int len)
{
    fwrite(text, 1, len, (FILE *)stream);
}

TextOutputDevOCR::TextOutputDevOCR(const char *fileName, bool discardDiagA)
{
    text = nullptr;
    discardDiag = discardDiagA;
    textEOL = defaultEndOfLine();
    textPageBreaks = true;
    ok = true;
    minColSpacing1 = minColSpacing1_default;

    // open file
    needClose = false;
    if (fileName) {
        if (!strcmp(fileName, "-")) {
            outputStream = stdout;
#if defined(_WIN32) || defined(__CYGWIN__)
            // keep DOS from munging the end-of-line characters
            _setmode(fileno(stdout), O_BINARY);
#endif
        } else if ( ( outputStream = openFile(fileName, "wb") ) ){
            needClose = true;
        } else {
            error(errIO, -1, "Couldn't open text file '{0:s}'", fileName);
            ok = false;
            actualText = nullptr;
            return;
        }
        outputFunc = &TextOutputDev_outputToFile;
    } else {
        outputStream = nullptr;
    }

    // set up text object
    text = new TextPageOCR(discardDiagA);
    actualText = new ActualTextOCR(text);

    splash = NULL;
    doc = NULL;
    display_header = true;
    display_left_margin = true;
    display_right_margin = true;
    display_footer = true;
}

TextOutputDevOCR::TextOutputDevOCR(TextOutputFunc func, void *stream, bool discardDiagA)
{
    outputFunc = func;
    outputStream = stream;
    needClose = false;
    discardDiag = discardDiagA;
    text = new TextPageOCR(discardDiagA);
    actualText = new ActualTextOCR(text);
    textEOL = defaultEndOfLine();
    textPageBreaks = true;
    ok = true;
    minColSpacing1 = minColSpacing1_default;

    splash = NULL;
    doc = NULL;
    display_header = true;
    display_left_margin = true;
    display_right_margin = true;
    display_footer = true;
}

TextOutputDevOCR::~TextOutputDevOCR()
{
    if (needClose) {
        fclose((FILE *)outputStream);
    }
    
    if (text) {
        text->decRefCnt();
    }

    delete actualText;

    if(splash){
        delete splash;
    }
}

void TextOutputDevOCR::startPage(int pageNum, GfxState *state, XRef *xref)
{
    text->startPage(state);
}

void TextOutputDevOCR::endPage()
{
    if(doc == NULL){
        error(errInternal, -1, __FILE__ ":TextOutputDevOCR::endPage: Invalid doc pointer");
    }

    text->endPage();
    text->coalesce(minColSpacing1);

    if (outputStream) {
        text->dump(outputStream, outputFunc, textEOL, textPageBreaks, display_header, display_left_margin, display_right_margin, display_footer);
    }
}

void TextOutputDevOCR::restoreState(GfxState *state)
{
    text->updateFont(state);
}

void TextOutputDevOCR::updateFont(GfxState *state)
{
    text->updateFont(state);
}

void TextOutputDevOCR::beginString(GfxState *state, const GooString *s) { }

void TextOutputDevOCR::endString(GfxState *state) { }

void TextOutputDevOCR::incCharCount(int nChars)
{
    text->incCharCount(nChars);
}

void TextOutputDevOCR::beginActualText(GfxState *state, const GooString *t)
{
    actualText->begin(state, t);
}

void TextOutputDevOCR::endActualText(GfxState *state)
{
    actualText->end(state);
}

void TextOutputDevOCR::setMergeCombining(bool merge)
{
    text->setMergeCombining(merge);
}

////////////////////////////////////////////////////////////////////////////////////////////////

// The following code that flushed the glyph cache after each page was tested on ~500 Open Access scientific PDF files
// to test if the same font (i.e., font id + font name + bold/italic + unicode) was modified on a per-page verses per-document level. No per-page
// modifications were found. This lets us skip redundant OCR classifications (since once we have seen a given glyph we can lookup its classification
// results)
//void TextOutputDevOCR::endPage()
//{
//  TextOutputDev::endPage();

  // Out of an abundance of caution, flush the glyph cache to make sure glyphs are re-predicted for every page in
  // a PDF document. Are there any examples "in the wild" of PDF documents that change the assigned Unicode
  // value for a glyph for a subset of pages but use the origina Unicode value for a different subset of pages?
  // If this is not possible, then we *don't* need to flush the cache, which will speed up the text conversion process!
//  corrected_glyphs.clear();
//}

void TextOutputDevOCR::drawChar(GfxState *state, double x, double y,
			     double dx, double dy,
			     double originX, double originY,
			     CharCode c, int nBytes, const Unicode *u, int uLen) {

  Unicode *replacement_u = NULL;
  
  if( (uLen > 0) && (u != NULL) ){

    // There is a unicode value associated with this glyph. Note that uLen > 1 indicates multiple characters being processed with a single call
    // to drawChar().
    replacement_u = new Unicode[uLen];
    memcpy(replacement_u, u, uLen*sizeof(Unicode));
  }
  else{

    // There is *no* unicode value associated with this glyph. While xpdf always appears to provide a unicode
    // value, there is at least one example (see PMC3792639/d-69-01889.pdf) for which Poppler does *not* provide
    // a unicode value. In this case, use the character code 'c' as a surrogate unicode value.
    uLen = 1;

    replacement_u = new Unicode[uLen];
    replacement_u[0] = c;
  }

  // uLen is the length of the Unicode array measured in increments of sizeof(Unicode) == 4 bytes
  if(splash){

    // The current font being rendered
    GfxFont *const font = state->getFont().get();

    const std::string font_tag = font->getTag();
    const std::optional<std::string> font_name = font->getName();
    
    const bool valid_font = !font_tag.empty() && font_name.has_value();

    const std::string font_id = font_name.value() + "+" + font_tag;

    if(valid_font){

      // For (rare) cases where uLen > 1, we are processing multiple characters. Loop over each unicode value
      // and classify the characters independantly.
      for(int unicode_index = 0;unicode_index < uLen;++unicode_index){

        const CharCode local_c = (uLen == 1) ? c : (replacement_u[unicode_index] & 0xFF);
        
        Glyph g(font_id, local_c, replacement_u[unicode_index]);

        if( font->isItalic() ){
            g.set_italic();
        }

        if( font->isBold() ){
            g.set_bold();
        }

        // Have we seen this glyph before?
        std::unordered_map<Glyph, std::pair<CharCode, Unicode> >::const_iterator glyph_iter = corrected_glyphs.find(g);

        if( glyph_iter != corrected_glyphs.end() ){
            
            // Setting the character code is *not* needed (since we're generating UTF-8 output?).
            //if(unicode_index == 0){
            //    c = glyph_iter->second.first;
            //}

            replacement_u[unicode_index] = glyph_iter->second.second; // <-- *Only* the unicode value is used to generate the output text
        }
        else{

            const double glyph_width = state->getHDPI();
            const double glyph_height = state->getVDPI();

            const double glyph_pt_size = 50; // Use a font size smaller than glyph_dpi to avoid clipping fonts <-- hand tuned based on bmp clipping warnings!

            // Step 1: Render glyph to a bitmap stored as a vector (with row first indexing).
            // The unwrapped bitmap is indexed by glyph[y*glyph_width + x]
            const std::pair< std::vector<unsigned char>, std::pair<int /*width*/, int /*height*/> > glyph_bmp = 
                renderGlyphToBitmap(splash, state, local_c, glyph_width, glyph_height, glyph_pt_size);
            
            const int bitmap_width = glyph_bmp.second.first;
            const int bitmap_height = glyph_bmp.second.second;

            bool clipped_bottom_boundary = false;
            bool clipped_top_boundary = false;
            bool clipped_left_boundary = false;
            bool clipped_right_boundary = false;

            #ifdef WARN_ON_CLIP
            // Print warnings if the font glyph extents beyond the edge of the bitmap

            for(int i = 0;i < bitmap_width;++i){

                if(glyph_bmp.first[0 + i] != 255){
                    clipped_bottom_boundary = true;
                }

                if(glyph_bmp.first[(bitmap_height - 1)*bitmap_width + i] != 255){
                    clipped_top_boundary = true;
                }
                }

                for(int i = 0;i < bitmap_height;++i){

                if(glyph_bmp.first[i*bitmap_width + 0] != 255){
                    clipped_left_boundary = true;
                }

                if(glyph_bmp.first[i*bitmap_width + (bitmap_width - 1)] != 255){
                    clipped_right_boundary = true;
                }
            }

            if(clipped_bottom_boundary){
                fprintf(stderr, "Clipped bottom for %c (%d)\n", c, c);
            }

            if(clipped_top_boundary){
                fprintf(stderr, "Clipped top for %c (%d)\n", c, c);
            }

            if(clipped_left_boundary){
                fprintf(stderr, "Clipped left for %c (%d)\n", c, c);
            }

            if(clipped_right_boundary){
                fprintf(stderr, "Clipped right for %c (%d)\n", c, c);
            }
            #endif // WARN_ON_CLIP

            bool is_empty_glyph = true;

            for(std::vector<unsigned char>::const_iterator i = glyph_bmp.first.begin();is_empty_glyph && ( i != glyph_bmp.first.end() );++i){
                is_empty_glyph = (*i == 0xFF); // Are all the pixels white (i.e., the background color)?
            }

            if(!is_empty_glyph){ // Only output and/or classifiy glyphs that have a least *one* pixel set

                if(dump_glyphs){

                    const char unprintable_code = '!';

                    // Note that isgraph() " checks for any printable character *except* space."
                    printf("#code = %c (%d);unicode = 0x%x;width = %d;height = %d;italic = %s;bold = %s;clipped left = %s;clipped right = %s;clipped top = %s;clipped bottom = %s\n", 
                        (isgraph(local_c) ? local_c : unprintable_code), c, replacement_u[unicode_index], bitmap_width, bitmap_height, (g.is_italic() ? "true" : "false"), (g.is_bold() ? "true" : "false"),
                        (clipped_left_boundary ? "true" : "false"), (clipped_right_boundary ? "true" : "false"), (clipped_top_boundary ? "true" : "false"), (clipped_bottom_boundary ? "true" : "false")
                        );

                    for(int local_y = bitmap_height - 1;local_y >= 0;--local_y){ // Invert the bitmap for human readability when debugging
                        for(int local_x = 0;local_x < bitmap_width;++local_x){

                            if(local_x > 0){
                                printf(",");
                            }
                    
                            printf("%d", int(glyph_bmp.first[local_y*bitmap_width + local_x]));
                        }
                    
                        printf("\n");
                    }
                }

                if(infer_glyphs){

                    // Step 2: Predict the unicode value from the glyph bitmap
                    const std::unordered_map<Unicode, float> pred = glyph_classifier.predict(glyph_bmp.first);

                    Unicode best_unicode = 0x0;
                    float best_p = 0.0;
                    float self_p = 0.0;

                    for(std::unordered_map<Unicode, float>::const_iterator iter = pred.begin();iter != pred.end();++iter){

                        if(iter->second > best_p){

                            best_p = iter->second;
                            best_unicode = iter->first;
                        }

                        if(iter->first == replacement_u[unicode_index]){
                            self_p = iter->second;
                        }
                    }
                    
                    // Step 3: Replace Unicode value (the character code, 'c', is *not* used to generate the text output)
                    if(replacement_u[unicode_index] != best_unicode){
                    
                        // If the reported unicode value is a control code, then accept the highest probability guess for a replacement symbol.
                        // Otherwise, do not attempt to correct reported glyphs that are too similar to the predicted glyph to allow reliable classification
                        if( iscntrl(replacement_u[unicode_index]) || 
                            ( (best_p >= prediction_probability_threshold) && (self_p <= self_probability_threshold) ) ){
                        
                            // Optional logging of changes
                            //fprintf(stderr, "Changed reported unicode glyph %#x (%f) to %#x (%f)\n", u[unicode_index], self_p, best_unicode, best_p);

                            const string predicted_str = utf8_to_string(best_unicode);

                            if( iscntrl(replacement_u[unicode_index]) ){
                                // Don't attempt to print a control character (like '\n' or '\b')
                                cerr << "Reported control-character (0x" << std::hex << replacement_u[unicode_index] << std::dec;
                            }
                            else{

                                const string reported_str = utf8_to_string(replacement_u[unicode_index]);

                                cerr << "Reported " << reported_str << " (0x" << std::hex << replacement_u[unicode_index] << std::dec;
                            }

                            cerr << "; " << self_p << "); predicted " << predicted_str << " (0x" << std::hex << best_unicode << std::dec << "; " << best_p << ")" << endl;

                            /////////////////////////////////////////////////////////////////////////
                            // Overwrite the reported unicode value with the predicted unicode value
                            replacement_u[unicode_index] = best_unicode;
                        }
                    }

                    corrected_glyphs[g] = std::make_pair(local_c, replacement_u[unicode_index]);
                }
            }
            else{ // Empty glyph -- make sure it is being mapped to white space
                // Does this empty glyph correspond to white space? If not, assume that this is a space
                // See page 3 of 4llo/PMC3910112/NIHMS507713-supplement-1.pdf for an egregious example!
                if( !isspace(local_c) ){
                    replacement_u[unicode_index] = 0x20;
                }
            }
        }
      }
    }
  }

  // Unicode contains a number of code points that represent *multiple* characters (i.e., "1/2", "1/4", ligatures "IJ", "ffi", etc.) 
  // Replace ligatures (i.e., multi-character glyphs) with a single unicode codepoint per character.
  deque<Unicode> ligature_buffer;

  for(int i = 0;i < uLen;++i){
    switch(replacement_u[i]){
        // List of unicode ligatures from: https://en.wikipedia.org/wiki/List_of_Unicode_characters
        // See also: https://en.wikipedia.org/wiki/Ligature_(writing)#Stylistic_ligatures
        case 0xc6:
            ligature_buffer.push_back('A');
            ligature_buffer.push_back('E');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with A + E" << endl;
            break;
        case 0xe6:
            ligature_buffer.push_back('a');
            ligature_buffer.push_back('e');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with a + e" << endl;
            break;
        case 0x132:
            ligature_buffer.push_back('I');
            ligature_buffer.push_back('J');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with I + J" << endl;
            break;
        case 0x133:
            ligature_buffer.push_back('i');
            ligature_buffer.push_back('j');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with i + j" << endl;
            break;
        case 0x152:
            ligature_buffer.push_back('O');
            ligature_buffer.push_back('E');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with O + E" << endl;
            break;
        case 0x153:
            ligature_buffer.push_back('o');
            ligature_buffer.push_back('e');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with o + e" << endl;
            break;
        case 0x1d6b:
            ligature_buffer.push_back('u');
            ligature_buffer.push_back('e');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with u + e" << endl;
            break;
        case 0xa732:
            ligature_buffer.push_back('A');
            ligature_buffer.push_back('A');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with A + A" << endl;
            break;
        case 0xa733:
            ligature_buffer.push_back('a');
            ligature_buffer.push_back('a');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with a + a" << endl;
            break;
        case 0xa734:
            ligature_buffer.push_back('A');
            ligature_buffer.push_back('O');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with A + O" << endl;
            break;
        case 0xa735:
            ligature_buffer.push_back('a');
            ligature_buffer.push_back('o');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with a + o" << endl;
            break;
        case 0xa736:
            ligature_buffer.push_back('A');
            ligature_buffer.push_back('U');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with A + U" << endl;
            break;
        case 0xa737:
            ligature_buffer.push_back('a');
            ligature_buffer.push_back('u');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with a + u" << endl;
            break;
        case 0xa738:
            ligature_buffer.push_back('A');
            ligature_buffer.push_back('V');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with A + V" << endl;
            break;
        case 0xa739:
            ligature_buffer.push_back('a');
            ligature_buffer.push_back('v');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with a + v" << endl;
            break;
        case 0xa73c:
            ligature_buffer.push_back('A');
            ligature_buffer.push_back('Y');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with A + Y" << endl;
            break;
        case 0xa73d:
            ligature_buffer.push_back('a');
            ligature_buffer.push_back('y');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with a + y" << endl;
            break;
        case 0xab63:
            ligature_buffer.push_back('u');
            ligature_buffer.push_back('o');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with u + o" << endl;
            break;
        case 0xfb00:
            ligature_buffer.push_back('f');
            ligature_buffer.push_back('f');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with f + f" << endl;
            break;
        case 0xfb01:
            ligature_buffer.push_back('f');
            ligature_buffer.push_back('i');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with f + i" << endl;
            break;
        case 0xfb02:
            ligature_buffer.push_back('f');
            ligature_buffer.push_back('l');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with f + l" << endl;
            break;
        case 0xfb03:
            ligature_buffer.push_back('f');
            ligature_buffer.push_back('f');
            ligature_buffer.push_back('i');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with f + f + i" << endl;
            break;
        case 0xfb04:
            ligature_buffer.push_back('f');
            ligature_buffer.push_back('f');
            ligature_buffer.push_back('l');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with f + f + l" << endl;
            break;
        case 0xfb05:
            ligature_buffer.push_back('f');
            ligature_buffer.push_back('t');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with f + t" << endl;
            break;
        case 0xfb06:
            ligature_buffer.push_back('s');
            ligature_buffer.push_back('t');
            cerr << "Replaced " << utf8_to_string(replacement_u[i]) << " with s + t" << endl;
            break;
        default:
            ligature_buffer.push_back(replacement_u[i]); // Not a ligature
            break;
    };
  }

  if( int( ligature_buffer.size() ) != uLen){
    
    // Replace the current unicode array with the expanded array that explicitly replaces
    // unicode ligatures with single-character glyphs
    delete [] replacement_u;

    uLen = ligature_buffer.size();
    replacement_u = new Unicode[uLen];

    for(int i = 0;i < uLen;++i){
        replacement_u[i] = ligature_buffer[i];
    }
  }

  // Add the potentially-modified unicode for writing to the output stream/file
  actualText->addChar(state, x, y, dx, dy, c, nBytes, replacement_u, uLen);

  delete [] replacement_u; // Allocated at the top of this function
}

void TextOutputDevOCR::startDoc(PDFDoc *docA, const char* m_model_filename, const double &m_best_threshold, const double &m_self_threshold, const bool m_dump_glyphs)
{
  if(splash){

    delete splash;
    splash = NULL;
  }

  doc = docA;

  // page color
  SplashColor white = {0xFF, 0xFF, 0xFF};

  splash = new SplashOutputDev(splashModeRGB8, 1, false, white);

  splash->startDoc(doc);

  dump_glyphs = m_dump_glyphs;

  if(m_model_filename && m_model_filename[0]){

    glyph_classifier.load_param(m_model_filename);
    
    prediction_probability_threshold = m_best_threshold; // Highest inference probability must be greater than this threshold
    self_probability_threshold = m_self_threshold; // Inference probability of the reported glyph must be less than this threshold

    infer_glyphs = true; // Infer glyph unicode values using the glyph classifier
  }
  else{

    prediction_probability_threshold = 1.0;
    self_probability_threshold = 0.0;

    infer_glyphs = false; // No inference but do generate glyph bitmaps for training
  }
}

std::pair< std::vector<unsigned char>, std::pair<int /*width*/, int /*height*/> > 
  TextOutputDevOCR::renderGlyphToBitmap(SplashOutputDev *m_splash, GfxState *state, const CharCode c, const double glyph_width, const double glyph_height, const double glyph_pt_size)
{
  PDFRectangle pageBox = PDFRectangle(0, 0, glyph_width, glyph_height);
  GfxState *tmpState = new GfxState(glyph_width, glyph_height, &pageBox, 0 /*rotate*/, false /*upside down*/);
  //GfxColor font_color;
  
  // Black font (since background is white)
  //font_color.c[0] = 0x0; font_color.c[1] = 0x0; font_color.c[2] = 0x0;

  tmpState->setFont(state->getFont(), glyph_pt_size);
  //tmpState->setFillColor(&font_color);

  if(doc == NULL){
    error(errInternal, -1, __FILE__ ":TextOutputDevOCR::renderGlyphToBitmap: doc == NULL");
  }

  m_splash->startPage(1 /*pageNum*/, tmpState, doc->getXRef());
  m_splash->updateFont(tmpState);
  
  // The fill and stroke flag values should be explored to maximize glyph recognition accuracy
  // Offset the char rendering to avoid cutting off glyphs that extend beyond
  m_splash->drawChar(tmpState, glyph_width*0.18, glyph_height*0.25, glyph_width, glyph_height, 0, 0, c, 0 /*dummy*/, NULL /*dummy*/, 0 /*dummy*/);

  m_splash->endPage(); // Essential to invoke final compositing step that performs antialiasing

  SplashBitmap *bitmap = m_splash->getBitmap(); // Copy to return

  const int width = bitmap->getWidth();
  const int height = bitmap->getHeight();

  std::vector<unsigned char> ret(width*height);

  for(int y = height - 1;y >= 0;--y){ // Invert the bitmap for human readability when debugging
    for(int x = 0;x < width;++x){

      SplashColor pixel; // RGB

      bitmap->getPixel(x, y, pixel);

      // Row first indexing
      ret[y*width + x] = pixel[0];
    }
  }

  if(tmpState){
    delete tmpState;
  }

  return std::make_pair( ret, std::make_pair(width, height) );
}

void WordCluster::align_baselines()
{
    const double overlap_threshold = 0.5; // In fraction of fontSize

    //sort( begin(), end(), SortWordsByBBox() );
    sort( begin(), end(), SortWordsInReadingOrder() );

    // Start from the begining of the cluster and force words that overlap in the secondary coordinate (i.e. Y for rot=0) 
    // to share the same baseline
    PDFRectangle prev_bbox(std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(), 
        std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest());

    double prev_baseline = std::numeric_limits<double>::lowest();

    for(WordCluster::iterator i = begin();i != end();++i){

        switch(rot){
            case 0:

                // DEBUG
                //cerr << "\"" << (*i)->getText()->c_str() << "\" -> x:" << minX << ".." << maxX << "; y:" << minY << ".." << maxY << "; baseline = " << (*i)->getBaseline() 
                //    << "; font size = " << (*i)->getFontSize() << ";rot = " << (*i)->getRotation() << endl;

                // Does the current word overlap the previous word in Y?
                if( prev_bbox.yMax() <= (*i)->yMin() ){ // No overlap

                    prev_bbox = *(*i);
                    prev_baseline = (*i)->getBaseline();
                }
                else{ // Overlap
                    
                    if( (*i)->getBaseline() != prev_baseline ){

                        const double dy = prev_baseline - (*i)->getBaseline();

                        if( fabs(dy) < overlap_threshold * (*i)->getFontSize() ){

                            // DEBUG
                            //cerr << "\tAligning current baseline (" << (*i)->getBaseline() << ") to previous baseline (" << prev_baseline << ") with dy = " << dy << endl;
                            (*i)->yMin() += dy;
                            (*i)->yMax() += dy;
                            (*i)->setBaseline(prev_baseline);                            
                        }
                        //else{
                            // DEBUG
                            //cerr << "\tDid not align current baseline (" << (*i)->getBaseline() << ") to previous baseline (" << prev_baseline << ") with dy = " << dy 
                            //    << "; overlap threshold = " << overlap_threshold * (*i)->getFontSize() << endl;
                        //}
                    }
                }
                break;
            case 1:
                // Does the current word overlap the previous word in X?
                if( prev_bbox.xMax() <= (*i)->xMin() ){ // No overlap

                    prev_bbox = *(*i);
                    prev_baseline = (*i)->getBaseline();
                }
                else{ // Overlap
                    
                    if( (*i)->getBaseline() != prev_baseline ){

                        const double dx = prev_baseline - (*i)->getBaseline();

                        if( fabs(dx) < overlap_threshold * (*i)->getFontSize() ){

                            (*i)->xMin() += dx;
                            (*i)->xMax() += dx;

                            (*i)->setBaseline(prev_baseline);
                        }
                    }
                }
                break;
            case 2:
                // Does the current word overlap the previous word in Y?
                if( prev_bbox.yMax() <= (*i)->yMin() ){ // No overlap

                    prev_bbox = *(*i);
                    prev_baseline = (*i)->getBaseline();
                }
                else{ // Overlap
                    
                    if( (*i)->getBaseline() != prev_baseline ){

                        const double dy = prev_baseline - (*i)->getBaseline();

                        if( fabs(dy) < overlap_threshold * (*i)->getFontSize() ){

                            (*i)->yMin() += dy;
                            (*i)->yMax() += dy;

                            (*i)->setBaseline(prev_baseline);
                        }
                    }
                }
                break;
            case 3:
                // Does the current word overlap the previous word in X?
                if( prev_bbox.xMax() <= (*i)->xMin() ){ // No overlap

                    prev_bbox = *(*i);
                    prev_baseline = (*i)->getBaseline();
                }
                else{ // Overlap
                    
                    if( (*i)->getBaseline() != prev_baseline ){

                        const double dx = prev_baseline - (*i)->getBaseline();

                        if( fabs(dx) < overlap_threshold * (*i)->getFontSize() ){

                            (*i)->xMin() += dx;
                            (*i)->xMax() += dx;
                            (*i)->setBaseline(prev_baseline);
                        }
                    }
                }
                break;
            default:
                error(errInternal, -1, __FILE__ ":WordCluster::align_baselines: Invalid rotation");
        };
    }
}

void WordCluster::merge_word_fragments()
{
    sort( begin(), end(), SortWordsInReadingOrder() );

    WordCluster::iterator iter = begin();
    bool has_merged = false;

    while( iter != end() ){

        WordCluster::iterator next_iter = iter + 1;

        if( next_iter == end() ){
            break;
        }

        // To merge iter and next_iter, they must
        // (a) share the same rotation
        // (b) share the same baseline
        // (c) be separated by less than a space?
        if( ( (*iter)->getRotation() == (*next_iter)->getRotation() ) &&
            ( (*iter)->getBaseline() == (*next_iter)->getBaseline() ) ){

            const double min_font_size = std::min( (*iter)->getFontSize(), (*next_iter)->getFontSize() );

            switch(rot){
                case 0:
                    if( ((*next_iter)->xMin() - (*iter)->xMax() ) <= minWordSpacing*min_font_size ){

                        (*iter)->append(*next_iter); // append() marks the underlying pointer for deletion but does not remove cluster element

                        next_iter = next_iter + 1;
                        has_merged = true;
                    }
                    break;
                case 1:
                    if( ( (*next_iter)->yMin() - (*iter)->yMax() ) <= minWordSpacing*min_font_size ){

                        (*iter)->append(*next_iter); // append() marks the underlying pointer for deletion but does not remove cluster element

                        next_iter = next_iter + 1;
                        has_merged = true;
                    }
                    break;
                case 2:
                    if( ( (*iter)->xMin() - (*next_iter)->xMax() ) <= minWordSpacing*min_font_size ){

                        (*iter)->append(*next_iter); // append() marks the underlying pointer for deletion but does not remove cluster element

                        next_iter = next_iter + 1;
                        has_merged = true;
                    }
                    break;
                case 3:
                    if( ( (*iter)->yMin() - (*next_iter)->yMax() ) <= minWordSpacing*min_font_size ){

                        (*iter)->append(*next_iter); // append() marks the underlying pointer for deletion but does not remove cluster element

                        next_iter = next_iter + 1;
                        has_merged = true;
                    }
                    break;
                default:
                    error(errInternal, -1, __FILE__ ":WordCluster::merge_word_fragments: Invalid rotation");
            };
        }

        iter = next_iter;
    }

    if(has_merged){
        
        sort( begin(), end(), SortWordsValid() );

        while( !empty() && ( back()->is_valid() == false) ){
            pop_back();
        }
    }
}

// The line word density is defined as the ratio:
//      (word area)/(per-line bounding box area)
// Unlike the cluster bounding box-based word density, this definition
// does not include the empty space before or after a line. The goal is to
// provide a metric to improve the classification of TEXT versus DATA clusters.
// Since baselines that only have a single word would lead to line word densities ~ 1
// (which can happen with text annotations in figures), require that a baseline have at least two words.
double WordCluster::line_multiword_density() const
{
    struct LineDensityInfo
    {
        PDFRectangle bbox;
        double word_area;
        unsigned int num_word;

        LineDensityInfo():
            word_area(0.0), num_word(0){
        };
    };

    std::unordered_map<double /*baseline*/, LineDensityInfo> info;

    for(std::deque<TextWordOCR*>::const_iterator i = begin();i != end();++i){
        
        LineDensityInfo &local = info[ (*i)->getBaseline() ];

        if(local.num_word == 0){
            local.bbox = *(*i);
        }
        else{
            local.bbox |= *(*i);
        }

        ++local.num_word;
        local.word_area += (*i)->area();
    }

    double total_line_area = 0.0;
    double total_word_area = 0.0;

    for( std::unordered_map<double /*baseline*/, LineDensityInfo>::const_iterator i = info.begin();i != info.end();++i){

        if(i->second.num_word > 1){ // Require more than one word per line

            total_word_area += i->second.word_area;
            total_line_area += i->second.bbox.area();
        }
    }

    return (total_line_area == 0.0) ? 0.0 : total_word_area/total_line_area;
};

// The count_cluster_words() function uses a more nuanced definition of what consititues a "word" that simply
// returning the size of a cluster ( i.e., size() ). For the purposed of count_cluster_words(), a "word" must contain
// at least one character that matches [a-z|A-Z] (excluding formating metadata characters). The goal of this word definition
// is to improve the classification of table/figure data from other types of clusters.
size_t WordCluster::count_cluster_words() const
{
    size_t ret = 0;

    for(std::deque<TextWordOCR*>::const_iterator i = begin();i != end();++i){
        ret += (*i)->is_alphabetic_word() ? 1 : 0;
    }

    return ret;
}
