#ifndef __CLASSIFICATION
#define __CLASSIFICATION

#include <deque>
#include <vector>
#include <unordered_map>
#include <fstream> // Forest serialization
#include <random>
#include <math.h>
#include <stdlib.h>
#include "glyph_classifier.h"

class RandomForest : public GlyphClassifier
{	
	public:

        struct Direction
        {
            Direction()
            {
                left = right = 0;
            };

            unsigned int left;
            unsigned int right;
        };

		struct TreeNode
		{
            Direction branch; // left or right (or the predicted value for branch tips)
			std::pair<FeatureIndex /*feature index*/, FeatureType /*threshold*/> boundary;
			
            TreeNode()
			{
			};

			inline bool is_leaf() const
			{
				return branch.left == branch.right;
			};

            inline void set_prediction(const PredictionType m_pred)
            {
                branch.left = branch.right = m_pred;
            };

            inline PredictionType get_prediction() const
            {
                return PredictionType(branch.left);
            };
		};

	private:
    
        typedef unsigned int PredictionIndex;
		typedef std::deque<TreeNode> Tree; 

		std::vector<Tree> forest;
		size_t forest_size;
        size_t forest_leaf;
        float forest_data_bag;
        float forest_feature_bag;

        // A lightweight structure to enable random sampling of
        // features and depended variables without modifying the
        // input structures.
        struct XY
        {
            const Features *x;
            PredictionIndex y;
            float weight;
            bool is_left;

            XY() : x(nullptr), y(0x0), weight(1.0f), is_left(false)
            {
            };

            XY(const Features *m_x, const PredictionIndex m_y, const float m_weight = 1.0f) : 
                x(m_x), y(m_y), weight(m_weight), is_left(false)
            {
                if(m_x == nullptr){
                    error(errInternal, -1, __FILE__ ":XY::XY(): m_x == nullptr");
                }

                if(m_weight < 0.0){
                    error(errInternal, -1, __FILE__ ":XY::XY(): m_weight < 0.0");
                }
            };

            inline bool operator<(const XY &m_rhs) const
            {
                // Allow sorting on pointer addressed for the feature data to
                // enable more predictable (i.e., faster) memory access
                return (x < m_rhs.x);
            };
        };

        struct IsLeft
        {
            inline bool operator()(const XY &m_xy) const
            {
                return (m_xy.is_left);
            };
        };

        bool best_split(std::pair<FeatureIndex, FeatureType> &m_boundary, std::vector<unsigned int> &m_left, 
            const size_t &m_leaf, 
            const std::vector<XY>::const_iterator &m_begin, 
            const std::vector<XY>::const_iterator &m_end,
            const std::vector<FeatureIndex>::const_iterator &m_feature_begin,
            const std::vector<FeatureIndex>::const_iterator &m_feature_end,
            const size_t m_num_class);

		void build_tree(Tree &m_tree, 
			std::vector<XY>::iterator m_data_begin /*copy*/,
			std::vector<XY>::iterator m_data_end /*copy*/,
            const std::vector<FeatureIndex>::const_iterator &m_feature_begin,
            const std::vector<FeatureIndex>::const_iterator &m_feature_end,
            const std::vector<PredictionType> &m_index_to_class);
			
        PredictionType predict_tree(const Tree &m_tree, const Features &m_features) const;
        PredictionType class_arg_max(const std::vector<XY>::const_iterator &m_data_begin, const std::vector<XY>::const_iterator &m_data_end) const;
	public:
	
    RandomForest() :
        forest_size(0),
        forest_leaf(0),
        forest_data_bag(0.0),
        forest_feature_bag(0.0)
        {
        };

		RandomForest(const size_t &m_forest_size, 
			const size_t &m_forest_leaf, 
			const float &m_forest_data_bag,
            const float &m_forest_feature_bag) :
			forest_size(m_forest_size),
			forest_leaf(m_forest_leaf),
			forest_data_bag(m_forest_data_bag),
            forest_feature_bag(m_forest_feature_bag)
		{
		};
		
		void build(const std::vector<PredictionType> &m_data,
			const std::vector<const Features*> &m_features, 
            unsigned int m_seed /*copy*/,
            const bool m_verbose = false);
		
        // Return the probability of each class
		std::unordered_map<PredictionType, float> predict(const Features &m_features) const;

        // Serialize the parameters of a trained random forest model as binary data
        void write_param(const std::string &m_filename) const;
        void load_param(const std::string &m_filename);
};

// A random_shuffle-like function that uses rand_r() for thread safety
template <class T>
void randomize(const T &m_begin, const T &m_end, std::mt19937 &m_rand_engine)
{
	const size_t len = m_end - m_begin;
	std::uniform_int_distribution<size_t> uniform_dist(0, len - 1);

	for(size_t i = 0;i < len;++i){
	
		// Generate a thread safe random number between [0, len)
		size_t index = uniform_dist(m_rand_engine);

		std::swap( *(m_begin + i), *(m_begin + index) );
	}
}

#endif // __CLASSIFICATION
