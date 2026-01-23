#ifndef __GLYPH_CLASSIFIER
#define __GLYPH_CLASSIFIER

#include <unordered_map>
#include <vector>
#include <string>
#include <fstream>
#include <zlib.h>

#include "Error.h" // Poppler does not use C++ exceptions

class GlyphClassifier
{
    public:
        enum {
            INVALID_ALGORITHM = 0x0,
            RANDOM_FOREST = 0x1, 
            MULTILAYER_PERCEPTRON = 0x2
        };

        typedef unsigned short int FeatureIndex; // This limits the size of feature vectors
        typedef unsigned char FeatureType;
        typedef unsigned int PredictionType;
        typedef std::vector<FeatureType> Features;

        // Make sure that we don't break the RandomForest class optimization that stores the predicted value in the branch direction variable
        static_assert(sizeof(PredictionType) <= sizeof(unsigned int), "GlyphClassifier::PredictionType must be small enough to store an unsigned int");

    public:
        
        virtual ~GlyphClassifier() = default;

        // Return the probability of each unicode class
        virtual std::unordered_map<PredictionType, float> predict(const Features &m_features) const = 0;

        // Serialize the parameters of a trained model from a file of binary parameters
        virtual void load_param(const std::string &m_filename) = 0;

};

// Helper function to read the first four bytes of a binary parameter file and return the algorithm id
inline unsigned int get_algorithm_id(const std::string &m_filename)
{
    gzFile fin = gzopen( m_filename.c_str(), "rb");

	if(fin == nullptr){
		return GlyphClassifier::INVALID_ALGORITHM;
	}
    
    unsigned int id;

    if(gzread(fin, &id, sizeof(id)) != sizeof(id)){
		return GlyphClassifier::INVALID_ALGORITHM;
	}

    gzclose(fin);

    return id;
};

#endif // __GLYPH_CLASSIFIER

