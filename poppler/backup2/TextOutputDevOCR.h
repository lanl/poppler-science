//========================================================================
//
// TextOutputDev.h
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
// Copyright (C) 2006 Ed Catmur <ed@catmur.co.uk>
// Copyright (C) 2007, 2008, 2011, 2013 Carlos Garcia Campos <carlosgc@gnome.org>
// Copyright (C) 2007, 2017 Adrian Johnson <ajohnson@redneon.com>
// Copyright (C) 2008, 2010, 2015, 2016, 2018, 2019, 2021 Albert Astals Cid <aacid@kde.org>
// Copyright (C) 2010 Brian Ewins <brian.ewins@gmail.com>
// Copyright (C) 2012, 2013, 2015, 2016 Jason Crain <jason@aquaticape.us>
// Copyright (C) 2013 Thomas Freitag <Thomas.Freitag@alfa.de>
// Copyright (C) 2018 Klarälvdalens Datakonsult AB, a KDAB Group company, <info@kdab.com>. Work sponsored by the LiMux project of the city of Munich
// Copyright (C) 2018 Sanchit Anand <sanxchit@gmail.com>
// Copyright (C) 2018, 2020, 2021 Nelson Benítez León <nbenitezl@gmail.com>
// Copyright (C) 2019, 2022 Oliver Sander <oliver.sander@tu-dresden.de>
// Copyright (C) 2019 Dan Shea <dan.shea@logical-innovations.com>
// Copyright (C) 2020 Suzuki Toshiya <mpsuzuki@hiroshima-u.ac.jp>
// Copyright (C) 2024, 2025 Stefan Brüns <stefan.bruens@rwth-aachen.de>
// Copyright (C) 2024, 2025 g10 Code GmbH, Author: Sune Stolborg Vuorela <sune@vuorela.dk>
//
// To see a description of the changes please see the Changelog file that
// came with your tarball or type make ChangeLog if you are building from git
//
//========================================================================

#ifndef TEXTOUTPUTDEV_H
#define TEXTOUTPUTDEV_H

#include "poppler-config.h"
#include "poppler_private_export.h"
#include <cstdio>
#include "GfxFont.h"
#include "GfxState.h"
#include "OutputDev.h"

// JDG
#include "random_forest.h"
#include <unordered_set>
#include <list>
#include "Page.h" // For PDFRectangle

class GooString;
class Gfx;
class GfxFont;
class GfxState;
class UnicodeMap;

class TextWordOCR;
class TextPageOCR;

// Borrowed from HTMLGen -- JDG
class PDFDoc;
class SplashOutputDev;

//------------------------------------------------------------------------

typedef void (*TextOutputFunc)(void *stream, const char *text, int len);

enum EndOfLineKind
{
    eolUnix, // LF
    eolDOS, // CR+LF
    eolMac // CR
};

//------------------------------------------------------------------------
// TextFontInfoOCR
//------------------------------------------------------------------------

class POPPLER_PRIVATE_EXPORT TextFontInfoOCR
{
public:
    explicit TextFontInfoOCR(const GfxState *state);
    ~TextFontInfoOCR();

    TextFontInfoOCR(const TextFontInfoOCR &) = delete;
    TextFontInfoOCR &operator=(const TextFontInfoOCR &) = delete;

    bool matches(const GfxState *state) const;
    bool matches(const TextFontInfoOCR *fontInfo) const;
    bool matches(const Ref *ref) const;

    // Get the font ascent, or a default value if the font is not set
    double getAscent() const;

    // Get the font descent, or a default value if the font is not set
    double getDescent() const;

    // Get the writing mode (0 or 1), or 0 if the font is not set
    int getWMode() const;

    // Get the font name (which may be NULL).
    const GooString *getFontName() const { return fontName; }

    // Get font descriptor flags.
    bool isFixedWidth() const { return flags & fontFixedWidth; }
    bool isSerif() const { return flags & fontSerif; }
    bool isSymbolic() const { return flags & fontSymbolic; }
    bool isItalic() const { return flags & fontItalic; }
    bool isBold() const { return flags & fontBold; }

private:
    std::shared_ptr<GfxFont> gfxFont;
    GooString *fontName;
    int flags;

    friend class TextWordOCR;
    friend class TextPageOCR;
};

//------------------------------------------------------------------------
// TextWordOCR
//------------------------------------------------------------------------

class POPPLER_PRIVATE_EXPORT TextWordOCR : public PDFRectangle
{
public:
    // Constructor.
    TextWordOCR(const GfxState *state, int rotA, double fontSize);

    // Destructor.
    ~TextWordOCR();

    TextWordOCR &operator=(const TextWordOCR &) = delete;

    // Add a character to the word.
    void addChar(const GfxState *state, TextFontInfoOCR *fontA, double x, double y, double dx, double dy, int charPosA, int charLen, CharCode c, Unicode u);

    // Attempt to add a character to the word as a combining character.
    // Either character u or the last character in the word must be an
    // acute, dieresis, or other combining character.  Returns true if
    // the character was added.
    bool addCombining(const GfxState *state, TextFontInfoOCR *fontA, double fontSizeA, double x, double y, double dx, double dy, int charPosA, int charLen, CharCode c, Unicode u);

    // Get the TextFontInfoOCR object associated with a character.
    const TextFontInfoOCR *getFontInfo(int idx) const { return chars[idx].font; }

    size_t getLength(bool m_exclude_formatting = false) const
    {
        if(!m_exclude_formatting){
            return chars.size();
        }

        size_t ret = 0;

        for(std::deque<TextWordOCR::CharInfo>::const_iterator i = chars.begin();i != chars.end();++i){
            if( !i->is_formatting ){
                ++ret;
            }
        }

        return ret;
    };

    const Unicode *getChar(int idx) const { return &chars[idx].text; };
    GooString *getText() const;
    const GooString *getFontName(int idx) const { return chars[idx].font->fontName; };

    double getFontSize() const { return fontSize; };
    int getRotation() const { return rot; };
    bool getSpaceAfter() const { return spaceAfter; };
    void setSpaceAfter(const bool m_has_space) { spaceAfter = m_has_space; };

    double getBaseline() const { return base; };
    void setBaseline(const double &m_baseline) { base = m_baseline; };
    inline bool is_valid() const {return valid;};

    bool is_alphabetic_word() const;

    void append(TextWordOCR* m_rhs)
    {
        if(m_rhs == NULL){
            //throw __FILE__ ":TextWordOCR::append: Invalid m_rhs == NULL";
            error(errInternal, -1, ":TextWordOCR::append: Invalid m_rhs == NULL");
        }

        if(rot != m_rhs->rot){
            //throw __FILE__ ":TextWordOCR::append: Cannot merge words with different rotations";
            error(errInternal, -1, ":TextWordOCR::append: Cannot merge words with different rotations");
        }

        // Append  m_rhs to this word
        for(std::deque<TextWordOCR::CharInfo>::const_iterator i = m_rhs->chars.begin();i != m_rhs->chars.end();++i){
            chars.push_back(*i);
        }

        m_rhs->valid = false; // Mark the appended word for deletion
        
        switch(rot){
            case 0:
                x2 = m_rhs->x2;
                break;
            case 1:
                y2 = m_rhs->y2;
                break;
            case 2:
                x1 = m_rhs->x1;
                break;
            case 3:
                y1 = m_rhs->y1;
                break;
            default:
                //throw __FILE__ ":TextWordOCR::append: Invalid rotation";
                error(errInternal, -1, ":TextWordOCR::append: Invalid rotation");
        };
    };

    void undo_rotation()
    {
        static_cast<PDFRectangle&>(*this) = rotate_counter_clockwise(rot);

        rot = 0; // Update the rotation to be zero
    };

private:
    void setInitialBounds(TextFontInfoOCR *fontA, double x, double y);

    struct CharInfo
    {
        Unicode text;
        CharCode charcode;
        double edge;
        TextFontInfoOCR *font;
        bool is_formatting; // Does this character represent formating metadata (e.g., <sup></sup>, <sub></sub>, etc.)?

        CharInfo()
        {
            text = 0x0;
            charcode = 0x0;
            edge = 0.0;
            font = NULL;
            is_formatting = false;
        };

        CharInfo(Unicode m_text, CharCode m_charcode, double m_edge, TextFontInfoOCR* m_font, bool m_is_formatting = false):
            text(m_text), charcode(m_charcode), edge(m_edge), font(m_font), is_formatting(m_is_formatting)
        {

        };
    };

    int rot; // rotation, multiple of 90 degrees
             //   (0, 1, 2, or 3)
    int wMode; // horizontal (0) or vertical (1) writing mode
    double base; // baseline x or y coordinate
    double fontSize; // font size

    // Switched from std::vector to std::deque to allow push_front
    //std::vector<CharInfo> chars;
    std::deque<CharInfo> chars;

    int charPosEnd = 0;
    double edgeEnd = 0;

    bool spaceAfter; // set if there is a space between this
                     //   word and the next word on the line
    bool underlined;
    bool invisible; // whether we are invisible (glyphless)
    bool valid; // Is this word valid or waiting to be deleted? (JDG)

    friend class TextPageOCR;
};

class WordCluster : public std::deque<TextWordOCR*>
{
    public:
        typedef enum {
            TEXT,               // Narrative text
            DATA,               // Figure or table
            REFERENCE,          // Bibliographic references
            HEADER,             // Metadata from the top of the page (typically title, authors, etc.)
            LEFT_MARGIN,        // Metadata from the left margin of the page (typically line numbers, manuscript status and journal information)
            RIGHT_MARGIN,       // Metadata from the right margin of the page (typically journal information)
            FOOTER,             // Metadata from the bottom of the page (typically, journal information, date, page number, etc.)
            UNKNOWN             // Catchall category for any cluster that is unclassified
        } Category;

        WordCluster()
        {
            category = UNKNOWN;
            rot = 0;
        };

        WordCluster(TextWordOCR* m_ptr)
        {
            if(m_ptr == NULL){
                //throw __FILE__ ":WordCluster: Invalid m_ptr == NULL";
                error(errInternal, -1, ":WordCluster: Invalid m_ptr == NULL");
            }

            category = UNKNOWN;

            push_back(m_ptr);
            rot = m_ptr->getRotation();
        };

        double distance2(const WordCluster& m_rhs, const double &m_threshold_primary /*X for rot 0*/, const double &m_threshold_secondary /*Y for rot 0*/) const
        {
            double best_d2 = std::numeric_limits<double>::max();
        
            // compute the minimum distance between the words in each cluster
            for(std::deque<TextWordOCR*>::const_iterator iter_i = begin();iter_i != end();++iter_i){

                PDFRectangle bbox;

                #define PACK_BBOX(VECT, WORD_PTR) \
                    bbox = *(WORD_PTR); \
                    VECT[0] = std::make_pair(bbox.x1, bbox.y1); \
                    VECT[1] = std::make_pair(bbox.x1, bbox.y2); \
                    VECT[2] = std::make_pair(bbox.x2, bbox.y1); \
                    VECT[3] = std::make_pair(bbox.x2, bbox.y2);

                std::vector< std::pair<double, double> > bbox_i(4);

                PACK_BBOX(bbox_i, *iter_i);

                for(std::deque<TextWordOCR*>::const_iterator iter_j = m_rhs.begin();iter_j != m_rhs.end();++iter_j){

                    const double min_font_size = std::min( (*iter_i)->getFontSize(), (*iter_j)->getFontSize() );
                    const double max_primary = m_threshold_primary*min_font_size;
                    const double max_secondary = m_threshold_secondary*min_font_size;

                    std::vector< std::pair<double, double> > bbox_j(4);

                    PACK_BBOX(bbox_j, *iter_j);

                    for(std::vector< std::pair<double, double> >::const_iterator a = bbox_i.begin();a != bbox_i.end();++a){
                        for(std::vector< std::pair<double, double> >::const_iterator b = bbox_j.begin();b != bbox_j.end();++b){

                            const double dx = fabs(a->first - b->first);
                            const double dy = fabs(a->second - b->second);

                            // Only include bounding box points that satisfy the distance constraints
                            // Note that (a) all words in both clusters have the same rotation, and (b) the
                            // rotation value determines the mapping of X and Y to primary and secondary axes.
                            if( (rot == 0) || (rot == 2) ){
                                if( (dx > max_primary) || (dy > max_secondary) ){
                                    continue;
                                }
                            }
                            else{ // (rot == 1) || (rot == 3)
                                if( (dy > max_primary) || (dx > max_secondary) ){
                                    continue;
                                }
                            }

                            const double d2 = dx*dx + dy*dy;

                            if(d2 < best_d2){
                                best_d2 = d2;
                            }
                        }
                    }
                }
            }

            #undef PACK_BBOX

            return best_d2;
        };

        PDFRectangle getBBox() const
        {
            PDFRectangle ret( std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest() );
            
            for(std::deque<TextWordOCR*>::const_iterator i = begin();i != end();++i){
                ret |= *(*i); // Find the minimial bounding box
            }

            return ret;
        };

        double area() const
        {
            return getBBox().area();
        };

        double word_area() const
        {
            double ret = 0.0;

            for(std::deque<TextWordOCR*>::const_iterator i = begin();i != end();++i){
                ret += (*i)->area();
            }

            return ret;
        };

        // The line word density is defined as the ratio:
        //      (word area)/(per-line bounding box area)
        // Unlike the cluster bounding box-based word density, this definition
        // does not include the empty space before or after a line. The goal is to
        // provide a metric to improve the classification of TEXT versus DATA clusters.
        // Since baselines that only have a single word would lead to line word densities ~ 1
        // (which can happen with text annotations in figures), require that a baseline have at least two words.
        double line_multiword_density() const;
        size_t count_cluster_words() const;

        size_t num_line() const // Count the number of unique baseine values to determine the number of text lines
        {
            std::unordered_set<double> baselines;

            for(std::deque<TextWordOCR*>::const_iterator i = begin();i != end();++i){
                baselines.insert( (*i)->getBaseline() );
            }

            return baselines.size();
        };

        inline Category getCategory() const
        {
            return category;
        };

        inline void setCategory(const Category &m_category)
        {
            category = m_category;
        };

        inline int getRotation() const
        {
            return rot;
        };
    
        // Correct minor baseline discrepancies
        void align_baselines();

        // Combine word fragments that share the same baseline
        void merge_word_fragments();

        private:
            Category category;
            int rot; // For now, all words in a cluster must have the same rotation
};

//------------------------------------------------------------------------
// TextPageOCR
//------------------------------------------------------------------------

class POPPLER_PRIVATE_EXPORT TextPageOCR
{
public:
    // Constructor.
    explicit TextPageOCR(bool discardDiagA = false);

    TextPageOCR(const TextPageOCR &) = delete;
    TextPageOCR &operator=(const TextPageOCR &) = delete;

    void incRefCnt();
    void decRefCnt();

    // Start a new page.
    void startPage(const GfxState *state);

    // End the current page.
    void endPage();

    // Update the current font.
    void updateFont(const GfxState *state);

    // Begin a new word.
    void beginWord(const GfxState *state);

    // Add a character to the current word.
    void addChar(const GfxState *state, double x, double y, double dx, double dy, CharCode c, int nBytes, const Unicode *u, int uLen);

    // Add <nChars> invisible characters.
    void incCharCount(int nChars);

    // End the current word, sorting it into the list of words.
    void endWord();

    // Add a word, sorting it into the list of words.
    void addWord(TextWordOCR *word);

    // Coalesce strings that look like parts of the same line.
    void coalesce();
    void coalesce(double minColSpacing1);

    // JDG
    void coalesce_superscript_and_subscript();
    void cluster_words();
    
    void delete_invalid_words()
    {
        std::list<TextWordOCR*>::iterator i = ptr_pool.begin();

        while( i != ptr_pool.end() ){

            if( !(*i)->valid ){
                
                std::list<TextWordOCR*>::iterator reaper = i;
                ++i;

                delete (*reaper);
                ptr_pool.erase(reaper);
            }
            else{
                ++i;
            }
        }
    };

    // Dump contents of page to a file.
    void dump(void *outputStream, TextOutputFunc outputFunc, EndOfLineKind textEOL, bool pageBreaks,
        bool m_display_header,  bool m_display_left_margin, bool m_display_right_margin, bool m_display_footer);

    // If true, will combine characters when a base and combining
    // character are drawn on eachother.
    void setMergeCombining(bool merge);

private:
    // Destructor.
    ~TextPageOCR();

    void clear();
    int dumpFragment(const Unicode *text, int len, const UnicodeMap *uMap, GooString *s) const;
    
    bool discardDiag; // discard diagonal text
    bool mergeCombining; // merge when combining and base characters
                         // are drawn on top of each other

    double pageWidth, pageHeight; // width and height of current page
    TextWordOCR *curWord; // currently active string
    int charPos; // next character position (within content
                 //   stream)
    TextFontInfoOCR *curFont; // current font
    double curFontSize; // current font size
    int nest; // current nesting level (for Type 3 fonts)
    int nTinyChars; // number of "tiny" chars seen so far
    bool lastCharOverlap; // set if the last added char overlapped the
                          //   previous char
    bool diagonal; // whether the current text is diagonal

    std::list<TextWordOCR*> ptr_pool; // a "pool" of TextWordOCR pointers (including all rotations)
    int primaryRot; // primary rotation
    bool primaryLR; // primary direction (true means L-to-R,
                    //   false means R-to-L)
    std::deque<WordCluster> clusters;

    std::vector<std::unique_ptr<TextFontInfoOCR>> fonts; // all font info objects used on this page

    double lastFindXMin, // coordinates of the last "find" result
            lastFindYMin;
    bool haveLastFind;

    int refCnt;
};

//------------------------------------------------------------------------
// ActualTextOCR
//------------------------------------------------------------------------

class POPPLER_PRIVATE_EXPORT ActualTextOCR
{
public:
    // Create an ActualText
    explicit ActualTextOCR(TextPageOCR *out);
    ~ActualTextOCR();

    ActualTextOCR(const ActualTextOCR &) = delete;
    ActualTextOCR &operator=(const ActualTextOCR &) = delete;

    void addChar(const GfxState *state, double x, double y, double dx, double dy, CharCode c, int nBytes, const Unicode *u, int uLen);
    void begin(const GfxState *state, const GooString *text);
    void end(const GfxState *state);

private:
    TextPageOCR *text;

    std::unique_ptr<GooString> actualText; // replacement text for the span
    double actualTextX0;
    double actualTextY0;
    double actualTextX1;
    double actualTextY1;
    int actualTextNBytes;
};

//------------------------------------------------------------------------
// TextOutputDevOCR and helper classes
//------------------------------------------------------------------------

// Glyph metadata (the glyph bitmap is stored separately)
struct Glyph
{
  enum{
    BOLD = 1 << 0,
    ITALIC = 1 << 1
  };

  std::string id;
  CharCode c; // <-- CharCode == unsigned int
  Unicode u;
  unsigned char flags;

  Glyph()
  {
    c = 0;
    u = 0;
    flags = 0x0;
  };

  Glyph(const std::string &m_id, const CharCode m_c, const Unicode m_u):
    id(m_id), c(m_c), u(m_u)
  {
    flags = 0x0;
  };

  inline bool operator==(const Glyph &m_rhs) const
  {
    return (id == m_rhs.id) && (c == m_rhs.c) && (u == m_rhs.u) && (flags == m_rhs.flags);
  };

  inline void set_italic()
  {
    flags |= ITALIC;
  };

  inline bool is_italic() const
  {
    return (flags & ITALIC);
  };

  inline void set_bold()
  {
    flags |= BOLD;
  };

  inline bool is_bold() const
  {
    return (flags & BOLD);
  };
};

namespace std {
  template <>
  struct hash<Glyph> {
      std::size_t operator()(const Glyph& m_obj) const {
          // Combine hash values of individual members
          return std::hash<std::string>()(m_obj.id) ^ std::hash<CharCode>()(m_obj.c) ^ std::hash<Unicode>()(m_obj.u) ^ (std::hash<unsigned char>()(m_obj.flags) );
      }
  };
}

//------------------------------------------------------------------------
// TextOutputDevOCR
//------------------------------------------------------------------------

class POPPLER_PRIVATE_EXPORT TextOutputDevOCR : public OutputDev
{
public:
    static double minColSpacing1_default;

    // Open a text output file.  If <fileName> is NULL, no file is
    // written (this is useful, e.g., for searching text).  If
    // <physLayoutA> is true, the original physical layout of the text
    // is maintained.  If <rawOrder> is true, the text is kept in
    // content stream order.  If <discardDiag> is true, diagonal text
    // is removed from output.
    TextOutputDevOCR(const char *fileName, bool discardDiagA = false);

    // Create a TextOutputDev which will write to a generic stream.  If
    // <physLayoutA> is true, the original physical layout of the text
    // is maintained.  If <rawOrder> is true, the text is kept in
    // content stream order.  If <discardDiag> is true, diagonal text
    // is removed from output.
    TextOutputDevOCR(TextOutputFunc func, void *stream, bool discardDiagA = false);

    // Destructor.
    ~TextOutputDevOCR() override;

    // Check if file was successfully created.
    virtual bool isOk() { return ok; }

    //---- get info about output device

    // Does this device use upside-down coordinates?
    // (Upside-down means (0,0) is the top left corner of the page.)
    bool upsideDown() override { return true; }

    // Does this device use drawChar() or drawString()?
    bool useDrawChar() override { return true; }

    // Does this device use beginType3Char/endType3Char?  Otherwise,
    // text in Type 3 fonts will be drawn with drawChar/drawString.
    bool interpretType3Chars() override { return false; }

    // Does this device need non-text content?
    bool needNonText() override { return false; }

    // Does this device require incCharCount to be called for text on
    // non-shown layers?
    bool needCharCount() override { return true; }

    //----- initialization and control

    // Start a page.
    void startPage(int pageNum, GfxState *state, XRef *xref) override;

    // End a page.
    void endPage() override;

    //----- save/restore graphics state
    void restoreState(GfxState *state) override;

    //----- update text state
    void updateFont(GfxState *state) override;

    //----- text drawing
    void beginString(GfxState *state, const GooString *s) override;
    void endString(GfxState *state) override;
    void drawChar(GfxState *state, double x, double y, double dx, double dy, double originX, double originY, CharCode c, int nBytes, const Unicode *u, int uLen) override;
    void incCharCount(int nChars) override;
    void beginActualText(GfxState *state, const GooString *text) override;
    void endActualText(GfxState *state) override;

    //----- special access

    void startDoc(PDFDoc *docA, const char* m_model_filename, const double &m_best_threshold, const double &m_self_threshold, const bool m_dump_glyphs);

    // If true, will combine characters when a base and combining
    // character are drawn on eachother.
    void setMergeCombining(bool merge);

    static constexpr EndOfLineKind defaultEndOfLine()
    {
#if defined(_WIN32)
        return eolDOS;
#else
        return eolUnix;
#endif
    }
    void setTextEOL(EndOfLineKind textEOLA) { textEOL = textEOLA; };
    void setTextPageBreaks(bool textPageBreaksA) { textPageBreaks = textPageBreaksA; };
    double getMinColSpacing1() const { return minColSpacing1; };
    void setMinColSpacing1(double val) { minColSpacing1 = val; };
    void* getoutputStream() { return outputStream; };
    TextOutputFunc getoutputFunc() { return outputFunc; };
    EndOfLineKind gettextEOL() const {return textEOL; };
    bool gettextPageBreaks() const {return textPageBreaks; };

    void disable_header() {display_header = false;};
    void disable_left_margin() {display_left_margin = false;};
    void disable_right_margin() {display_right_margin = false;};
    void disable_footer() {display_footer = false;};

private:
    TextOutputFunc outputFunc; // output function
    void *outputStream; // output stream
    bool needClose; // need to close the output file?
                    //   (only if outputStream is a FILE*)
    TextPageOCR *text; // text for the current page

    double minColSpacing1; // see default value defined with same name at TextOutputDev.cc
    bool discardDiag; // Diagonal text, i.e., text that is not close to one of the
                      // 0, 90, 180, or 270 degree axes, is discarded. This is useful
                      // to skip watermarks drawn on top of body text, etc.
    bool ok; // set up ok?
    bool textPageBreaks; // insert end-of-page markers?

    EndOfLineKind textEOL; // type of EOL marker to use

    ActualTextOCR *actualText;

    SplashOutputDev *splash; // For rendering font glyphs as bitmaps in memory
    PDFDoc *doc;
    bool display_header;
    bool display_left_margin;
    bool display_right_margin;
    bool display_footer;

    std::unordered_map<Glyph, std::pair<CharCode, Unicode> > corrected_glyphs; // Cache the glyph-to-unicode mapping

    bool infer_glyphs; // true for inference, false for training
    bool dump_glyphs; // Write glyph bitmaps to stdout for training set generation
    RandomForest glyph_classifier;
    float prediction_probability_threshold; // Highest inference probability must be greater than this threshold
    float self_probability_threshold; // Inference probability of the reported glyph must be less than this threshold

    std::pair< std::vector<unsigned char>, std::pair<int /*width*/, int /*height*/> > 
        renderGlyphToBitmap(SplashOutputDev *m_splash, GfxState *state, const CharCode c, const double glyph_width, const double glyph_height, const double glyph_pt_size);
};


#endif
