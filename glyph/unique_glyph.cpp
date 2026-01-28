// Read glyph bitmaps (as gzipped compressed binary data) from input files and output a file containing the unique set of bitmaps
#include <stdlib.h>
#include <zlib.h>
#include <getopt.h>
#include <iostream>
#include <sstream>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <limits>

using namespace std;

#define     UNIQUE_GLYPH_VERSION        "0.2 September 18, 2025"

typedef unsigned int Unicode;

template <class T>
struct Bitmap : public std::vector<T>
{
    int width;
    int height;

    Bitmap()
    {
        clear();
    };

    Bitmap(const int m_width, const int m_height)
    {
        resize(m_width, m_height);
    };

    inline void clear()
    {
        width = height = 0;
        vector<T>::clear();
    };

    inline void resize(const int m_width, const int m_height)
    {
        width = m_width;
        height = m_height;

        if( (width < 0) || (height < 0)){
            throw __FILE__ ":Bitmap: Invalid width and/or height";
        }

        vector<T>::resize(width*height);
    };

    inline T pixel(const int m_width_index, const int m_height_index) const
    {
        const int index = m_height_index*width + m_width_index;

        if( (index < 0) || ( index >= vector<T>::size() )){
            throw __FILE__ ":Bitmap::pixel: Index out of bounds";
        }

        return (*this)[index];
    };

    inline T& pixel(const int m_width_index, const int m_height_index)
    {
        const int index = m_height_index*width + m_width_index;

        if( (index < 0) || ( index >= vector<T>::size() )){
            throw __FILE__ ":Bitmap::pixel: Index out of bounds";
        }

        return (*this)[index];
    };
};

// The std::hash<size_t>() is the identity hash (i.e., just returns the input)!
size_t hash_size_t(size_t m_obj)
{
    // From ChatGPT:
    // MurmurHash3 fmix64
    m_obj ^= m_obj >> 33; m_obj *= 0xff51afd7ed558ccdll;
    m_obj ^= m_obj >> 33; m_obj *= 0xc4ceb9fe1a85ec53ll;
    m_obj ^= m_obj >> 33;
    
    return m_obj;
}

template <class T>
std::size_t hash_function(const int m_width, const int m_height, const std::vector<T> &m_data)
{
    
    size_t ret = std::hash<int>()(m_width) ^ std::hash<int>()(m_height);

    size_t buffer = 0x0;
    size_t buffer_len = 0;

    for(typename std::vector<T>::const_iterator i = m_data.begin();i != m_data.end();++i){
        
        buffer = (buffer << 8*sizeof(T)) | *i;
        ++buffer_len;

        if( buffer_len == sizeof(buffer) ){

            // Include the index location since black and white images
            // will have many buffers that are all black or all white
            ret ^= hash_size_t(buffer + ( i - m_data.begin() ) );
            buffer = 0x0;
            buffer_len = 0;
        }
    }

    if(buffer_len > 0){

        ret ^= hash_size_t(buffer);
        buffer = 0x0;
        buffer_len = 0;
    }

    return ret;
}

namespace std {
    template <class T>
    struct hash< Bitmap<T> > {
        std::size_t operator()(const Bitmap<T>& m_obj) const {
            return hash_function(m_obj.width, m_obj.height, (vector<T>)m_obj);
        }
    };
}

typedef enum {
    BINARY_FORMAT,
    ASCII_FORMAT,
} FileFormat;

int main(int argc, char* argv[])
{
    try{

        const char* options = "o:?h";
        int config_opt = 0;
        int long_index = 0;

        struct option long_opts[] = {
            {"clean", false, &config_opt, 1},
            {"o.binary", false, &config_opt, 2},
            {"o.ascii", false, &config_opt, 3},
            {0,0,0,0} // Terminate options list
        };

        int opt_code;
        opterr = 0;

        string output_file;
        bool clean_up = false;
        bool print_usage = (argc == 1);
        FileFormat output_format = BINARY_FORMAT;
        deque<string> input_files;

        while( (opt_code = getopt_long( argc, argv, options, long_opts, &long_index) ) != EOF ){

            switch( opt_code ){
                case 0:
                    if(config_opt == 1){ // clean

                        clean_up = true;
                        break;
                    }

                    if(config_opt == 2){ // o.binary

                        output_format = BINARY_FORMAT;
                        break;
                    }

                    if(config_opt == 3){ // o.ascii

                        output_format = ASCII_FORMAT;
                        break;
                    }

                    cerr << "Unknown command line flag!" << endl;
                    return EXIT_SUCCESS;

                case 'o':
                    output_file = optarg;
                    break;
                case 'h':
                case '?':
                    print_usage = true;
                    break;
                default:
                    cerr << '\"' << (char)opt_code << "\" is not a valid option!" << endl;
                    return EXIT_SUCCESS;
            };
        }

        for(int i = optind;i < argc;i++){
            input_files.push_back(argv[i]);
        }

        if(print_usage){

            cerr << "Unique glyph version " << UNIQUE_GLYPH_VERSION << endl;
            cerr << "Usage:" << endl;
            cerr << "\t-o <output filename>" << endl;
            cerr << "\t[--o.binary (binary format) | --o.ascii (ascii format)]; default is binary" << endl;
            cerr << "\t[--clean (delete input files after extracting and writing unique glyph bitmaps)] (default is false)" << endl;

            return EXIT_SUCCESS;
        }

        if( output_file.empty() ){

            cerr << "Please specify an output filename" << endl;
            return EXIT_SUCCESS;
        }

        if( input_files.empty() ){

            cerr << "Please specify one or more input filenames" << endl;
            return EXIT_SUCCESS;
        }
        
        unordered_map< Bitmap<unsigned char>, unordered_map<Unicode, size_t/*count*/> > db;

        int global_width = -1; // All bitmaps must have the same dimensions
        int global_height = -1;

        for(deque<string>::const_iterator file_iter = input_files.begin();file_iter != input_files.end();++file_iter){

            cout << "Reading binary glyph data from " << *file_iter << endl;

            gzFile fin = gzopen(file_iter->c_str(), "rb");

            if(fin == NULL){

                cerr << "Unable to open " << *file_iter << " for reading binary data" << endl;
                return EXIT_FAILURE;
            }

            unordered_map< Bitmap<unsigned char>, unordered_map<Unicode, size_t/*count*/> > local_db;

            // Each binary glyph bitmap file contains zero or more bitmap records.
            // Each bitmap record contains:
            //  - unicode value (4 byte unsigned int)
            //  - Number of occurences of this unicode + bitmap combination    
            //  - bitmap_width (4 byte signed int)
            //  - bitmap_height (4 byte signed int)
            //  - bitmap_width*bitmap_height pixel value (each pixel is an unsigned char)

            while(true){ // Keep reading until we exhaust the available input data

                Unicode u;
                size_t glyph_count;
                int width;
                int height;
                unsigned char *buffer = NULL;

                int ret = gzread( fin, &u, sizeof(Unicode) );

                if(ret == 0){
                    break;
                }

                if( ret != sizeof(Unicode) ){
                    throw __FILE__ ": error reading unicode value";
                }

                if( gzread( fin, &glyph_count, sizeof(size_t) ) != sizeof(size_t) ){
                    throw __FILE__ ": error reading glyph_count";
                }

                if( gzread( fin, &width, sizeof(int) ) != sizeof(int) ){
                    throw __FILE__ ": error reading width";
                }

                if(width <= 0){
                    throw __FILE__ ": width <= 0";
                }

                if( gzread( fin, &height, sizeof(int) ) != sizeof(int) ){
                    throw __FILE__ ": error reading height";
                }

                if(height <= 0){
                    throw __FILE__ ": height <= 0";
                }

                if( (global_width < 0) || (global_height < 0) ){
                    global_width = width;
                    global_height = height;
                }
                else{
                    if(global_width != width){
                        throw __FILE__ ": global_width != width <-- all bitmaps must have the same width";
                    }

                    if(global_height != height){
                        throw __FILE__ ": global_height != height <-- all bitmaps must have the same height";
                    }
                }

                const size_t buffer_len = width*height;

                if( buffer_len > std::numeric_limits<unsigned int>::max() ){
                    throw __FILE__ ": buffer_len exceed maximum value of an unsigned int -- something strange has happened!";
                }

                buffer = new unsigned char[buffer_len];

                if(!buffer){
                    throw __FILE__ ": Unable to allocate bitmap buffer";
                }

                if( gzread(fin, buffer, sizeof(unsigned char)*buffer_len) != sizeof(unsigned char)*buffer_len ){
                    throw __FILE__ ": error reading bitmap data";
                }

                Bitmap<unsigned char> bmp(width, height);

                bmp.assign(buffer, buffer + buffer_len);

                if(buffer){

                    delete [] buffer;
                    buffer = NULL;
                }

                // Individual files may have repeated glyphs, avoid multiple-counting by
                // only storing the maximum count seen in a given file. The appearance of multiple glyphs per file is
                // a side-effect of the PDF parser saving unique glyphs per page (instead of per file). 
                size_t &count = local_db[bmp][u];

                count = max(glyph_count, count);
            }

            if(gzclose(fin) != Z_OK){
                throw __FILE__ "gzclose(fin) != Z_OK";
            }

            // Accumulate the information from this file
            for(unordered_map< Bitmap<unsigned char>, unordered_map<Unicode, size_t> >::const_iterator i = local_db.begin();i != local_db.end();++i){

                unordered_map<Unicode, size_t>& dst = db[i->first];

                for(unordered_map<Unicode, size_t>::const_iterator j = i->second.begin();j != i->second.end();++j){
                    dst[j->first] += j->second;
                }
            }
        }

        gzFile fout = gzopen(output_file.c_str(), (output_format == BINARY_FORMAT) ? "wb" : "wT" );

        if(fout == NULL){

            cerr << "Unable to open output file " << output_file << " for writing" << endl;
            return EXIT_SUCCESS;
        }

        size_t num_glyph_combo = 0;
        size_t num_multi_map = 0;

        if(output_format == BINARY_FORMAT){

            const size_t bmp_len = global_width*global_height;

            for(unordered_map< Bitmap<unsigned char>, unordered_map<Unicode, size_t/*count*/> >::const_iterator i = db.begin();i != db.end();++i){
               
                num_glyph_combo += i->second.size();
                num_multi_map += (i->second.size() > 1);
 
                for(unordered_map<Unicode, size_t>::const_iterator j = i->second.begin();j != i->second.end();++j){

                    if(gzwrite(fout, &(j->first), sizeof(Unicode)) != sizeof(Unicode) ){
                        throw __FILE__ ": Error writing unicode value";
                    }

                    if(gzwrite(fout, &(j->second), sizeof(size_t)) != sizeof(size_t) ){
                        throw __FILE__ ": Error writing glyph count";
                    }

                    if(gzwrite(fout, &global_width, sizeof(int)) != sizeof(int) ){
                        throw __FILE__ ": Error writing bitmap width";
                    }

                    if(gzwrite(fout, &global_height, sizeof(int)) != sizeof(int) ){
                        throw __FILE__ ": Error writing bitmap height";
                    }

                    if(gzwrite(fout, i->first.data(), sizeof(unsigned char)*bmp_len) != int(sizeof(unsigned char)*bmp_len) ){
                        throw __FILE__ ": Error writing bitmap data";
                    }
                }
            }
        }
        else{ //output_format == ASCII_FORMAT

            const char unprintable_code = '!';

            for(unordered_map< Bitmap<unsigned char>, unordered_map<Unicode, size_t> >::const_iterator i = db.begin();i != db.end();++i){
             	
                num_glyph_combo += i->second.size();
                num_multi_map += (i->second.size() > 1);
 
                for(unordered_map<Unicode, size_t >::const_iterator j = i->second.begin();j != i->second.end();++j){

                    const char c = j->first & 0xFF;

                    gzprintf(fout, "#code = %c;unicode = 0x%x;count = %zu\n", isgraph(c) ? c : unprintable_code, j->first, j->second);

                    // Invert the bitmap for human readability
                    for(int h = i->first.height - 1;h >= 0;--h){ 
                        for(int w = 0;w < i->first.width;++w){

                            if(w > 0){
                                gzprintf(fout, ",");
                            }

                            gzprintf( fout, "%d", i->first.pixel(w, h) );
                        }

                        gzprintf(fout, "\n");
                    }
                }
            }
        }

        cerr << "Found a total of " << db.size() << " bitmaps for " << num_glyph_combo << " unicode values" << endl;
        cerr << num_multi_map << " glyphs (" << (100.0*num_multi_map)/db.size() << "%) have multiple associated unicode values" << endl;
        
        if(num_glyph_combo > 0){
            cerr << "<|bmp|/|unicode|> = " << float( db.size() )/num_glyph_combo << " bmp per code" << endl;
        }

        if(gzclose(fout) != Z_OK){

            cerr << "Error writing output file detected by gzclose()" << endl;
            return EXIT_FAILURE;
        }

        if(clean_up){

            for(deque<string>::const_iterator file_iter = input_files.begin();file_iter != input_files.end();++file_iter){

                if(*file_iter == output_file){
                    continue; // A rather weak method for protecting the output file from getting deleted.
                }

                if(unlink( file_iter->c_str() ) != 0){
                    cerr << "Warning: unable to remove " << *file_iter << endl;
                }
            }
        }
    }
    catch(const char* error){
        cerr << "Caught the error: " << error << endl;
        return EXIT_FAILURE;
    }
    catch(...){
        cerr << "Caught an unhandled error" << endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

