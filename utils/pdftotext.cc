//========================================================================
//
// pdftotext.cc
//
// Copyright 1997-2003 Glyph & Cog, LLC
//
// Modified for Debian by Hamish Moffatt, 22 May 2002.
//
//========================================================================

//========================================================================
//
// Modified under the Poppler project - http://poppler.freedesktop.org
//
// All changes made under the Poppler project to this file are licensed
// under GPL version 2 or later
//
// Copyright (C) 2006 Dominic Lachowicz <cinamod@hotmail.com>
// Copyright (C) 2007-2008, 2010, 2011, 2017-2022, 2024 Albert Astals Cid <aacid@kde.org>
// Copyright (C) 2009 Jan Jockusch <jan@jockusch.de>
// Copyright (C) 2010, 2013 Hib Eris <hib@hiberis.nl>
// Copyright (C) 2010 Kenneth Berland <ken@hero.com>
// Copyright (C) 2011 Tom Gleason <tom@buildadam.com>
// Copyright (C) 2011 Steven Murdoch <Steven.Murdoch@cl.cam.ac.uk>
// Copyright (C) 2013 Yury G. Kudryashov <urkud.urkud@gmail.com>
// Copyright (C) 2013 Suzuki Toshiya <mpsuzuki@hiroshima-u.ac.jp>
// Copyright (C) 2015 Jeremy Echols <jechols@uoregon.edu>
// Copyright (C) 2017 Adrian Johnson <ajohnson@redneon.com>
// Copyright (C) 2018 Klarälvdalens Datakonsult AB, a KDAB Group company, <info@kdab.com>. Work sponsored by the LiMux project of the city of Munich
// Copyright (C) 2018 Adam Reichold <adam.reichold@t-online.de>
// Copyright (C) 2018 Sanchit Anand <sanxchit@gmail.com>
// Copyright (C) 2019 Dan Shea <dan.shea@logical-innovations.com>
// Copyright (C) 2019, 2021 Oliver Sander <oliver.sander@tu-dresden.de>
// Copyright (C) 2021 William Bader <williambader@hotmail.com>
// Copyright (C) 2022 kVdNi <kVdNi@waqa.eu>
//
// To see a description of the changes please see the Changelog file that
// came with your tarball or type make ChangeLog if you are building from git
//
//========================================================================

#include "config.h"
#include <poppler-config.h>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include "parseargs.h"
#include "printencodings.h"
#include "goo/GooString.h"
#include "goo/gmem.h"
#include "GlobalParams.h"
#include "Object.h"
#include "Stream.h"
#include "Array.h"
#include "Dict.h"
#include "XRef.h"
#include "Catalog.h"
#include "Page.h"
#include "PDFDoc.h"
#include "PDFDocFactory.h"
#include "TextOutputDevOCR.h"
#include "CharTypes.h"
#include "UnicodeMap.h"
#include "PDFDocEncoding.h"
#include "Error.h"
#include <string>
#include <sstream>
#include <iomanip>
#include "Win32Console.h"
#include "DateInfo.h"
#include <cfloat>

static int firstPage = 1;
static int lastPage = 0;
static double resolution = 72.0;
static int x = 0;
static int y = 0;
static int w = 0;
static int h = 0;
static double colspacing = TextOutputDevOCR::minColSpacing1_default;
static bool discardDiag = false;
static char textEncName[128] = "UTF-8"; // Changed the default encoding to UTF-8
static char textEOLStr[16] = "";
static bool noPageBreaks = false;
static char ownerPassword[33] = "\001";
static char userPassword[33] = "\001";
static char ocrModelFileName[256] = "";
static char ocrGlyphFileName[256] = "";
static double ocrBestThreshold = 0.25;
static double ocrSelfThreshold = 0.01;
static bool tagSection = false;
static bool tagSection_data = false;
static bool tagSection_header = false;
static bool tagSection_left_margin = false;
static bool tagSection_right_margin = false;
static bool tagSection_footer = false;
static bool tagSuper = false;
static bool tagSub = false;
static bool noHeader = false;
static bool noLeftMargin = false;
static bool noRightMargin = false;
static bool noFooter = false;
static bool splitLigature = false;
static bool quiet = false;
static bool printVersion = false;
static bool printHelp = false;
static bool printEnc = false;

static const ArgDesc argDesc[] = { { "-f", argInt, &firstPage, 0, "first page to convert" },
                                   { "-l", argInt, &lastPage, 0, "last page to convert" },
                                   { "-r", argFP, &resolution, 0, "resolution, in DPI (default is 72)" },
                                   { "-x", argInt, &x, 0, "x-coordinate of the crop area top left corner" },
                                   { "-y", argInt, &y, 0, "y-coordinate of the crop area top left corner" },
                                   { "-W", argInt, &w, 0, "width of crop area in pixels (default is 0)" },
                                   { "-H", argInt, &h, 0, "height of crop area in pixels (default is 0)" },
                                   { "-nodiag", argFlag, &discardDiag, 0, "discard diagonal text" },
                                   { "-enc", argString, textEncName, sizeof(textEncName), "output text encoding name" },
                                   { "-listenc", argFlag, &printEnc, 0, "list available encodings" },
                                   { "-eol", argString, textEOLStr, sizeof(textEOLStr), "output end-of-line convention (unix, dos, or mac)" },
                                   { "-nopgbrk", argFlag, &noPageBreaks, 0, "don't insert page breaks between pages" },
                                   { "-colspacing", argFP, &colspacing, 0,
                                     "how much spacing we allow after a word before considering adjacent text to be a new column, as a fraction of the font size (default is 0.7, old releases had a 0.3 default)" },
                                   { "-opw", argString, ownerPassword, sizeof(ownerPassword), "owner password (for encrypted files)" },
                                   { "-upw", argString, userPassword, sizeof(userPassword), "user password (for encrypted files)" },
                                   {"-ocr.model", argString, ocrModelFileName, sizeof(ocrModelFileName), "machine learning model parameters for OCR glyph classification"},
                                   {"-ocr.best_threshold", argFP, &ocrBestThreshold, 0, "minimum threshold for highest probability OCR inferred glyph"},
                                   {"-ocr.self_threshold", argFP, &ocrSelfThreshold, 0, "maximum threshold for probability of reported glyph"},
                                   {"-ocr.dump_glyphs", argString, ocrGlyphFileName, sizeof(ocrGlyphFileName), "write glyph bitmaps to file"},
                                   {"-noheader", argFlag, &noHeader, 0, "don't output page headers"},
                                   {"-noleftmargin", argFlag, &noLeftMargin, 0, "don't output left margin text"},
                                   {"-norightmargin", argFlag, &noRightMargin, 0, "don't output right margin text"},
                                   {"-nofooter", argFlag, &noFooter, 0, "don't output page footers"},
                                   {"-tag.section", argFlag, &tagSection, 0, "output HTML tags for all sections"},
                                   {"-tag.section.data", argFlag, &tagSection_data, 0, "output HTML tags for data (table/figure) sections"},
                                   {"-tag.section.header", argFlag, &tagSection_header, 0, "output HTML tags for header sections"},
                                   {"-tag.section.leftmargin", argFlag, &tagSection_left_margin, 0, "output HTML tags for left margin sections"},
                                   {"-tag.section.rightmargin", argFlag, &tagSection_right_margin, 0, "output HTML tags for right margin sections"},
                                   {"-tag.section.footer", argFlag, &tagSection_footer, 0, "output HTML tags for footer sections"},
                                   {"-tag.superscript", argFlag, &tagSuper, 0, "output HTML superscript tags"},
                                   {"-tag.subscript", argFlag, &tagSub, 0, "output HTML subscript tags"},
                                   {"-splitligature", argFlag, &splitLigature, 0, "decompose unicode ligatures into separate characters"},
                                   { "-q", argFlag, &quiet, 0, "don't print any messages or errors" },
                                   { "-v", argFlag, &printVersion, 0, "print copyright and version info" },
                                   { "-h", argFlag, &printHelp, 0, "print usage information" },
                                   { "-help", argFlag, &printHelp, 0, "print usage information" },
                                   { "--help", argFlag, &printHelp, 0, "print usage information" },
                                   { "-?", argFlag, &printHelp, 0, "print usage information" },
                                   {} };

int main(int argc, char *argv[])
{
    std::unique_ptr<PDFDoc> doc;
    std::unique_ptr<GooString> textFileName;
    std::optional<GooString> ownerPW, userPW;
    const UnicodeMap *uMap;
    Object info;
    bool ok;
    EndOfLineKind textEOL = TextOutputDevOCR::defaultEndOfLine();

    Win32Console win32Console(&argc, &argv);

    // parse args
    ok = parseArgs(argDesc, &argc, argv);
    
    if (colspacing <= 0 || colspacing > 10) {
        error(errCommandLine, -1, "Bogus value provided for -colspacing");
        return 99;
    }
    if (!ok || (argc < 2 && !printEnc) || argc > 3 || printVersion || printHelp) {
        fprintf(stderr, "pdftotext version %s\n", PACKAGE_VERSION);
        fprintf(stderr, "%s\n", popplerCopyright);
        fprintf(stderr, "%s\n", xpdfCopyright);
        if (!printVersion) {
            printUsage("pdftotext", "<PDF-file> [<text-file>]", argDesc);
        }
        if (printVersion || printHelp) {
            return 0;
        }
        return 99;
    }

    // read config file
    globalParams = std::make_unique<GlobalParams>();

    if (printEnc) {
        printEncodings();
        return 0;
    }

    GooString fileName(argv[1]);
    
    if (textEncName[0]) {

        // If the user is performing per-glyph OCR, issue a warning if they are *not* using UTF-8 text encoding
        if( ocrModelFileName[0] && ( (strlen(textEncName) != 5) || strncmp(textEncName, "UTF-8", 5) ) ){
            fprintf(stderr, "**Warning** Using a text encoding other than UTF-8 may break per-glyph OCR!\n");
        }

        globalParams->setTextEncoding(textEncName);
    }
    if (textEOLStr[0]) {
        if (!strcmp(textEOLStr, "unix")) {
            textEOL = eolUnix;
        } else if (!strcmp(textEOLStr, "dos")) {
            textEOL = eolDOS;
        } else if (!strcmp(textEOLStr, "mac")) {
            textEOL = eolMac;
        } else {
            fprintf(stderr, "Bad '-eol' value on command line\n");
        }
    }
    if (quiet) {
        globalParams->setErrQuiet(quiet);
    }

    // get mapping to output encoding
    if (!(uMap = globalParams->getTextEncoding())) {
        error(errCommandLine, -1, "Couldn't get text encoding");
        return 99;
    }

    // open PDF file
    if (ownerPassword[0] != '\001') {
        ownerPW = GooString(ownerPassword);
    }
    if (userPassword[0] != '\001') {
        userPW = GooString(userPassword);
    }

    if (fileName.cmp("-") == 0) {
        fileName = GooString("fd://0");
    }

    doc = PDFDocFactory().createPDFDoc(fileName, ownerPW, userPW);

    if (!doc->isOk()) {
        return 1;
    }

#ifdef ENFORCE_PERMISSIONS
    // check for copy permission
    if (!doc->okToCopy()) {
        error(errNotAllowed, -1, "Copying of text from this document is not allowed.");
        return 3;
    }
#endif

    // construct text file name
    if (argc == 3) {
        textFileName = std::make_unique<GooString>(argv[2]);
    } else if (fileName.cmp("fd://0") == 0) {
        error(errCommandLine, -1, "You have to provide an output filename when reading from stdin.");
        return 99;
    } else {
        const char *p = fileName.c_str() + fileName.getLength() - 4;
        if (!strcmp(p, ".pdf") || !strcmp(p, ".PDF")) {
            textFileName = std::make_unique<GooString>(fileName.c_str(), fileName.getLength() - 4);
        } else {
            textFileName = fileName.copy();
        }
        textFileName->append(".txt");
    }

    // get page range
    if (firstPage < 1) {
        firstPage = 1;
    }
    if (lastPage < 1 || lastPage > doc->getNumPages()) {
        lastPage = doc->getNumPages();
    }
    if (lastPage < firstPage) {
        error(errCommandLine, -1, "Wrong page range given: the first page ({0:d}) can not be after the last page ({1:d}).", firstPage, lastPage);
        return 99;
    }

    // Changed from TextOutputDev to TextOutputDevOCR to enable per glyph OCR
    //TextOutputDev textOut(textFileName->c_str(), physLayout, fixedPitch, rawOrder, htmlMeta, discardDiag);
    TextOutputDevOCR textOut( textFileName->c_str() );

    if(discardDiag){
        textOut.disable_option(DISPLAY_DIAGONAL);
    }
    else{
        textOut.set_option(DISPLAY_DIAGONAL);
    }

    if(noHeader){
        textOut.disable_option(DISPLAY_HEADER);
    }
    else{
        textOut.set_option(DISPLAY_HEADER);
    }

    if(noLeftMargin){
        textOut.disable_option(DISPLAY_LEFT_MARGIN);
    }
    else{
        textOut.set_option(DISPLAY_LEFT_MARGIN);
    }

    if(noRightMargin){
        textOut.disable_option(DISPLAY_RIGHT_MARGIN);
    }
    else{
        textOut.set_option(DISPLAY_RIGHT_MARGIN);
    }

    if(noFooter){
        textOut.disable_option(DISPLAY_FOOTER);
    }
    else{
        textOut.set_option(DISPLAY_FOOTER);
    }

    if(noPageBreaks){
        textOut.disable_option(DISPLAY_PAGE_BREAKS);
    }
    else{
        textOut.set_option(DISPLAY_PAGE_BREAKS);
    }

    if(tagSuper){
         textOut.set_option(DISPLAY_SUPERSCRIPT_TAG);
    }
    else{
        textOut.disable_option(DISPLAY_SUPERSCRIPT_TAG);
    }

    if(tagSub){
         textOut.set_option(DISPLAY_SUBSCRIPT_TAG);
    }
    else{
        textOut.disable_option(DISPLAY_SUBSCRIPT_TAG);
    }

    if(tagSection_data || tagSection){
        textOut.set_option(DISPLAY_DATA_TAG);
    }

    if(tagSection_header || tagSection){
        textOut.set_option(DISPLAY_HEADER_TAG);
    }

    if(tagSection_left_margin || tagSection){
        textOut.set_option(DISPLAY_LEFT_MARGIN_TAG);
    }

    if(tagSection_right_margin || tagSection){
        textOut.set_option(DISPLAY_RIGHT_MARGIN_TAG);
    }

    if(tagSection_footer || tagSection){
        textOut.set_option(DISPLAY_FOOTER_TAG);
    }

    if(splitLigature){
        textOut.set_option(SPLIT_LIGATURE);
    }

    if (textOut.isOk()) {

        textOut.setTextEOL(textEOL);
        textOut.setMinColSpacing1(colspacing);

        textOut.startDoc(doc.get(), ocrModelFileName, ocrBestThreshold, ocrSelfThreshold, ocrGlyphFileName);

        if ((w == 0) && (h == 0) && (x == 0) && (y == 0)) {
            doc->displayPages(&textOut, firstPage, lastPage, resolution, resolution, 0, true, false, false);
        } else {
            for (int page = firstPage; page <= lastPage; ++page) {
                doc->displayPageSlice(&textOut, page, resolution, resolution, 0, true, false, false, x, y, w, h);
            }
        }

    } else {
        return 2;
    }

    return 0;
}
