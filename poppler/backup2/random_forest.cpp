#include "random_forest.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <list>
#include <unordered_map>
#include <math.h>

using namespace std;

// A way to potentially speed up the sorting and partitioning routines (which can consume a non-trivial
// amount of CPU time) is to use the g++-specific __gnu_parallel:: funcions instead
// of the std:: functions. Since __gnu_parallel:: is a drop in replacement for
// std::, include:
//#include <parallel/algorithm>
// which defines _GLIBCXX_PARALLEL_PARALLEL_H and enables parallel sorting
// Please note that approach will not work on non-g++ compilers (i.e. clang or intel).

#ifdef _GLIBCXX_PARALLEL_PARALLEL_H

	#define	SORT		__gnu_parallel::sort
	#define	PARTITION	__gnu_parallel::partition
#else

	#define	SORT		std::sort
	#define	PARTITION	std::partition
#endif // _GLIBCXX_PARALLEL_PARALLEL_H

struct sort_by_first
{
	inline bool operator()(const pair<FeatureType, unsigned int> &m_a, const pair<FeatureType, unsigned int> &m_b) const
	{
		return m_a.first < m_b.first;
	};
};

// Overload the '+=' operator for accumulating tree predictions
std::unordered_map<PredictionType, float> operator+=(std::unordered_map<PredictionType, float> &m_lhs, const std::unordered_map<PredictionType, float> &m_rhs)
{
	for(std::unordered_map<PredictionType, float>::const_iterator i = m_rhs.begin();i != m_rhs.end();++i){
		m_lhs[i->first] += i->second;
	}

	return m_lhs;
}

void normalize_predictions(std::unordered_map<PredictionType, float> &m_predictions)
{
	float norm = 0.0;

	for(std::unordered_map<PredictionType, float>::const_iterator i = m_predictions.begin();i != m_predictions.end();++i){
		norm += i->second;
	}

	if(norm > 0.0){
		
		norm = 1.0/norm;

		for(std::unordered_map<PredictionType, float>::iterator i = m_predictions.begin();i != m_predictions.end();++i){
			i->second *= norm;
		}
	}
}

void RandomForest::build(const vector<PredictionType> &m_data, 
	const vector< const vector<FeatureType>* > &m_features,
	unsigned int m_seed /*copy*/,
    const bool m_verbose)
{
	if(forest_size == 0){
		
		// when number of trees is less than the number if workers, an
		// individual worker may have nothing to do.
		return;
	}
	
	if( (forest_data_bag <= 0.0) || (forest_data_bag > 1.0) ){
		error(errInternal, -1, __FILE__ ":RandomForest::build: Please specify 0 < forest_data_bag <= 1.0");
	}
	
	if( (forest_feature_bag <= 0.0) || (forest_feature_bag > 1.0) ){
		error(errInternal, -1, __FILE__ ":RandomForest::build: Please specify 0 < forest_feature_bag <= 1.0");
	}

	const unsigned int num_data = m_data.size();
	
    if( num_data != m_features.size() ){
		error(errInternal, -1, __FILE__ ":RandomForest::build: Number of data points != number of feature vectors");
    }

    if(num_data == 0){
		error(errInternal, -1, __FILE__ ":RandomForest::build: No training data!");
    }

	// Make sure that all of the data have the same number of features
	// and the same number of response values
	const unsigned int num_features = m_features.front()->size();

	if(num_features == 0){
		error(errInternal, -1, __FILE__ ":RandomForest::build: No features!");
	}

	vector<XY> data_ptr;

	data_ptr.resize(num_data);
	
	// Use the balanced weight heuristic as described in: https://scikit-learn.org/stable/modules/generated/sklearn.utils.class_weight.compute_class_weight.html
	#define BALANCED_WEIGHT

	#ifdef BALANCED_WEIGHT

	// Count the number of each class
	unordered_map<PredictionType, unsigned int> class_count;
	
	for(vector<PredictionType>::const_iterator i = m_data.begin();i != m_data.end();++i){
		++class_count[*i];
	}

	//i->second = float(num_data)/(parent_weight.size() * i->second);
	#endif // BALANCED_WEIGHT

	unordered_map<PredictionType, PredictionIndex> class_to_index;
	vector<PredictionType> index_to_class;

	// Identify invariant features (so we can exclude them from the tree building process)
	vector<bool> constant_features(num_features, true);

	for(size_t i = 0;i < num_data;++i){
		
		if( num_features != m_features[i]->size() ){
			error(errInternal, -1, __FILE__ ":RandomForest::build: Variable number of features not allowed");
		}

        // Pointers to the features and data that can be sorted without modifying the
		// input order
		data_ptr[i].x = m_features[i];

		if( class_to_index.find(m_data[i]) == class_to_index.end() ){

			class_to_index[ m_data[i] ] = class_to_index.size();
			index_to_class.push_back(m_data[i]);
		}

		data_ptr[i].y = class_to_index[ m_data[i] ];

		#ifdef BALANCED_WEIGHT
		// Both inverse weighting and 1/sqrt weighting yeild similar results
		//data_ptr[i].weight = 1.0f/class_count[ m_data[i] ];
		data_ptr[i].weight = 1.0f/sqrt( class_count[ m_data[i] ] );
		#endif // BALANCED_WEIGHT

		for(size_t j = 0;j < num_features;++j){
			if( (*(m_features[0]))[j] != (*(m_features[i]))[j]){
				constant_features[j] = false;
			}
		}
	}

	forest.resize(forest_size);

	vector<unsigned int> feature_index;
	
	feature_index.reserve(num_features);

	for(unsigned int i = 0;i < num_features;++i){

		if(constant_features[i] == false){
			feature_index.push_back(i);
		}
	}

	if( feature_index.empty() ){
		error(errInternal, -1, __FILE__ ":RandomForest::build: No (varying) training data!");
	}

	const unsigned int num_bagged_data = max(1U, (unsigned int)(forest_data_bag*num_data) );
	const unsigned int num_bagged_features = max(1U, (unsigned int)(forest_feature_bag*feature_index.size()) );

	string info;

    if(m_verbose){
	    cout << "\tBuilding trees: ";
    }	

	size_t num_tree_complete = 0;

	#pragma omp parallel
	{
		vector<unsigned int> local_feature_index(feature_index);
		vector<XY> local_data_ptr(data_ptr);
		time_t profile = time(NULL);

		unsigned int local_seed = 0x0;

		#pragma omp critical
		{
			// Each OpenMP thread uses a different random number seed
			m_seed += 1;
			local_seed = m_seed;
		}
		
		std::mt19937 rand_engine(local_seed);

		#pragma omp for
		for(size_t i = 0;i < forest_size;++i){

			randomize( local_data_ptr.begin(), local_data_ptr.end(), rand_engine);
			randomize( local_feature_index.begin(), local_feature_index.end(), rand_engine);

			// After the random selection step, sort the data pointers and feature indicies in
			// ascending order for better memory access
			sort(local_data_ptr.begin(), local_data_ptr.begin() + num_bagged_data);
			sort(local_feature_index.begin(), local_feature_index.begin() + num_bagged_features);

			// Remove any existing tree data (for the case when we are reusing a RandomForest object
			// for multiple training runs)
			forest[i].clear();

			RandomForest::build_tree(forest[i], 
				local_data_ptr.begin(), local_data_ptr.begin() + num_bagged_data,
				local_feature_index.begin(), local_feature_index.begin() + num_bagged_features,
				index_to_class);

			#pragma omp critical
			if(m_verbose){
				
				++num_tree_complete;

				profile = time(NULL) - profile;

				for(string::const_iterator j = info.begin();j != info.end();++j){
					cout << '\b';
				}

				for(string::const_iterator j = info.begin();j != info.end();++j){
					cout << ' ';
				}

				for(string::const_iterator j = info.begin();j != info.end();++j){
					cout << '\b';
				}

				stringstream ssout;

				ssout << (100.0*num_tree_complete)/forest_size << "% in " << profile << " sec";

				info = ssout.str();

				cout << info;

				cout.flush();

				profile = time(NULL);
			}
		}
	}

	if(m_verbose){
		cout << endl;
	}
}

PredictionType RandomForest::class_arg_max(const vector<XY>::const_iterator &m_data_begin, const vector<XY>::const_iterator &m_data_end) const
{
	// Accumulate the prediction weights
	std::unordered_map<PredictionType, unsigned int> count;

	for(vector<XY>::const_iterator i = m_data_begin;i != m_data_end;++i){
		++count[i->y];
	}

	unsigned int best_count = 0;
	PredictionType best_class = 0x0;

	for(std::unordered_map<PredictionType, unsigned int>::const_iterator i = count.begin();i != count.end();++i){

		if(i->second > best_count){

			best_count = i->second;
			best_class = i->first;
		}
	}

	if(best_count == 0){
		error(errInternal, -1, __FILE__ ":RandomForest::class_arg_max: The best class count is zero");
	}

	return best_class;
}

void RandomForest::build_tree(Tree &m_tree, 
	vector<XY>::iterator m_data_begin /*copy*/,
	vector<XY>::iterator m_data_end /*copy*/,
	const vector<unsigned int>::const_iterator &m_feature_begin,
	const vector<unsigned int>::const_iterator &m_feature_end,
	const vector<PredictionType> &m_index_to_class)
{
	if(m_data_end <= m_data_begin){
		error(errInternal, -1, __FILE__ ":RandomForest::build_tree: No data!");
	}
	
	if(m_feature_end <= m_feature_begin){
		error(errInternal, -1, __FILE__ ":RandomForest::build_tree: No features!");
	}

	const unsigned int num_data = m_data_end - m_data_begin;
	
	TreeNode local;
	
	m_tree.push_back(local);
	
	// Do we have enough data to make a split?
	if(num_data <= forest_leaf){
		
		m_tree.back().prediction = m_index_to_class[ class_arg_max(m_data_begin, m_data_end) ];
		return;
	}
	
	// Search for the partition that obtains the smallest *weighted* mean square error
	pair<unsigned int, FeatureType> best_boundary;
	vector<unsigned int> best_left;

	if( best_split(best_boundary, best_left, forest_leaf, m_data_begin, m_data_end,
		m_feature_begin, m_feature_end, m_index_to_class.size()) == false){
	
		// We could not find a valid split, collect the leaf member class counts
		m_tree.back().prediction = m_index_to_class[ class_arg_max(m_data_begin, m_data_end) ];
		return;
	}

	// Partition the data into left and right branches. Since a non-trivial amount of time
	// is spent paritioning the data, this code has some admittedly kludgy hacks to make it run as
	// fast as posible. A single bit (borrowed from the high bit of the index member 
	// variable) is used to indicate membership in the left hand set.	
	for(vector<unsigned int>::iterator i = best_left.begin();i != best_left.end();++i){
        (m_data_begin + *i)->is_left = true;
	}
	
	PARTITION( m_data_begin, m_data_end, IsLeft() );
	
	vector<XY>::iterator boundary_iter = m_data_begin + best_left.size();
	
	// Unset the sign bit so we can use the weight variable normally
	for(vector<XY>::iterator i = m_data_begin;i != boundary_iter;++i){
		i->is_left = false;
	}
	
	const unsigned int node = m_tree.size() - 1;
	
	// DEBUG
	//if(best_weight < 0.0){
	//	cerr << "best_weight = " << best_weight << endl;
	//	throw __FILE__ ": weight out of bounds!";
	//}

	m_tree[node].boundary = best_boundary;
	m_tree[node].left = m_tree.size();

	build_tree(m_tree, m_data_begin, boundary_iter,
		m_feature_begin, m_feature_end,
		m_index_to_class);

	m_tree[node].right = m_tree.size();

	build_tree(m_tree, boundary_iter, m_data_end,
		m_feature_begin, m_feature_end,
		m_index_to_class);
}

std::unordered_map<PredictionType, float> RandomForest::predict(
	const vector<FeatureType> &m_features) const
{
	std::unordered_map<PredictionType, float> ret;

	if( forest_size != forest.size() ){
		error(errInternal, -1, __FILE__ ":RandomForest::predict: forest_size != forest.size()");
	}
	
    if(forest_size == 0){
	   error(errInternal, -1, __FILE__ ":RandomForest::predict: No trees in the forest!");
    }

	// A quick benchmark suggests that parallelizing this for loop
	// is slower (by a factor of two) than the serial version.
	//#pragma omp parallel for reduction(+:sum)
	for(size_t i = 0;i < forest_size;++i){
		ret[ predict_tree(forest[i], m_features) ] += 1.0f;
	}
	
	normalize_predictions(ret);

	return ret;
}

PredictionType RandomForest::predict_tree( const Tree &m_tree,
	const vector<FeatureType> &m_features) const
{
	if( m_tree.empty() ){
		error(errInternal, -1, __FILE__ ":RandomForest::predict_tree: Empty tree!");
	}
	
	const unsigned int num_features = m_features.size();
	
	unsigned int index = 0;

	while(true){

		const TreeNode &node = m_tree[index];

		if( node.is_leaf() ){
			return node.prediction;
		}
		
		if(num_features <= node.boundary.first){
			error(errInternal, -1, __FILE__ ":RandomForest::predict_tree: Feature index out of bounds!");
		}

		index = (m_features[node.boundary.first] < node.boundary.second) ?
			node.left :
			node.right;
	}
	
	error(errInternal, -1, __FILE__ ":RandomRegressionForest::predict_tree: Should never get here!");
	
	return PredictionType();
}

// Return true if we found a valid split, false otherwise
bool RandomForest::best_split(pair<unsigned int, FeatureType> &m_boundary, vector<unsigned int> &m_left,
	const size_t &m_leaf, 
	const vector<XY>::const_iterator &m_data_begin, 
	const vector<XY>::const_iterator &m_data_end,
	const std::vector<unsigned int>::const_iterator &m_feature_begin,
    const std::vector<unsigned int>::const_iterator &m_feature_end,
	const size_t m_num_class)
{
	bool ret = false;
	
	if(m_data_end <= m_data_begin){
		error(errInternal, -1, __FILE__ ":best_split: No data!");
	}
	
	if(m_feature_end <= m_feature_begin){
		error(errInternal, -1, __FILE__ ":best_split: No features!");
	}

	const unsigned int num_data = m_data_end - m_data_begin;
	const unsigned int num_features_to_test = m_feature_end - m_feature_begin;
	
	if(m_leaf < 1){
		error(errInternal, -1, __FILE__ ":best_split: m_leaf < 1");
	}

	vector<float> parent_weight(m_num_class);
	float total_weight = 0.0f;

	for(vector<XY>::const_iterator i = m_data_begin;i != m_data_end;++i){

		// Count the number of each sample type
		parent_weight[i->y] += i->weight;
		total_weight += i->weight;
	}

	float parent_entropy = 0.0;

	for(vector<float>::const_iterator i = parent_weight.begin();i != parent_weight.end();++i){

		const float p = *i/total_weight;

		if(p > 0.0){
			parent_entropy -= p*log2f(p);
		}
	}

	if(parent_entropy <= 0.0){
		return false; // Can't split a pure state
	}

	float best_score = parent_entropy; // <-- A really large value!

	// DEBUG
	//cerr << "parent_entropy = " << parent_entropy << endl;
	//cerr << "num_data = " << num_data << endl;

	//#pragma omp parallel
	{
		// Store the values for the i^th independent feature. We can allocate
		// this memory outside the for loop, since the size does not change
		vector< pair<FeatureType, unsigned int> > feature_slice(num_data);

		float local_best_score = parent_entropy;
		pair<unsigned int, float> local_boundary;
		vector<unsigned int> local_left;
		
		// Test each feature and every possible boundary value within a feature 
		//#pragma omp for
		for(unsigned int f = 0;f < num_features_to_test;++f){

			for(unsigned int i = 0;i < num_data;++i){
				feature_slice[i] = 
					make_pair( (*((m_data_begin + i)->x))[*(m_feature_begin + f)], i);
			}

			// Sort the feature values in ascending order. The sort_by_first() function is an optimization
			// to avoid the default comparison behavior of std::pair (which compares both elements of pair, even
			// though we only need to compare by the first element)
			sort( feature_slice.begin(), feature_slice.end(), sort_by_first() );

			// To make the calculation efficient, track the running sum of y values in the left and 
			// right branches.
			vector<float> left_weight(m_num_class);
			vector<float> right_weight(m_num_class);
			
			// By default, all of the data starts in the *right* branch
			float num_left = 0;
			float num_right = total_weight;

			right_weight = parent_weight;
			
			for(unsigned int i = 0;i < num_data;++i){

				// Move data point i from the right branch to the left branch
				const PredictionIndex y = (m_data_begin + feature_slice[i].second)->y; // Make a copy since sizeof(PredictionType) < sizeof(PredictionType*)
				const float w = (m_data_begin + feature_slice[i].second)->weight;

				// Since the sums are unnormalized, we can simply remove a point from the right and
				// add it to the left
				right_weight[y] -= w;
				num_right -= w;

				left_weight[y] += w;
				num_left += w;

				if( (i < m_leaf) || (i >= (num_data - m_leaf) ) ){
					continue;
				}

				// Don't split on equal values! We can access element i + 1 of the
				// feature slice vector since m_leaf must be greater than 0 (and the
				// above test on i will trigger a "continue" at the end of the vector range)
				if(feature_slice[i].first == feature_slice[i + 1].first){
					continue;
				}
				
				if( (num_left <= 0.0) || (num_right <= 0.0) ){
					// Round off error can cause values < 0.0
					continue;
				}

				// Compute the information gain for this split
				float left_entropy = 0.0f;
				float right_entropy = 0.0f;

				#define ENTROPY(CLASS_WEIGHT, NORM, ENTROPY) \
					for(size_t INDEX = 0;INDEX < m_num_class;++INDEX){ \
					\
						const float p = CLASS_WEIGHT[INDEX]/NORM;\
						\
						if(p > 0.0f){ \
							ENTROPY -= p*log2f(p);\
						} \
					}

				ENTROPY(left_weight, num_left, left_entropy);
				ENTROPY(right_weight, num_right, right_entropy);

				const float entropy = (left_entropy*num_left + right_entropy*num_right)/total_weight;

				// DEBUG
				//cerr << "entropy = " << entropy << endl;
				//cerr << "\tlocal_best_score = " << local_best_score << endl;
				//cerr << "\tleft_entropy = " << left_entropy << "; L = " << num_left << endl;
				//cerr << "\tright_entropy = " << right_entropy << "; R = " << num_right << endl;

				if(entropy < local_best_score){

					local_best_score = entropy;
					
					// Place the boundary at the midpoint between the current and the
					// next feature value.
					local_boundary = make_pair(*(m_feature_begin + f), 
						0.5*(feature_slice[i].first + feature_slice[i + 1].first) );

					local_left.resize(i + 1);

					for(unsigned int j = 0;j <= i;++j){
						local_left[j] = feature_slice[j].second;
					}
					
					// If a data point is not in the left branch, it must be in the
					// right branch (so we don't need to explicitly store the data points
					// that belong to the right hand branch).
				}
			}
		}
		
		//#pragma omp critical
		if( (local_best_score < best_score) && !local_left.empty()){
			
			best_score = local_best_score;
			m_boundary = local_boundary;
			m_left = local_left;
			
			ret = true;
		}
	}
	
	return ret;
}

void RandomForest::write_param(const std::string &m_filename) const
{
	ofstream fout( m_filename.c_str(), ios::binary);

	if(!fout){
		cerr << "Unable to open " << m_filename << " for writing model parameters" << endl;
		error(errInternal, -1, __FILE__ ":RandomForest::write_param:");
	}

	// Write the forest parameters
	fout.write( (const char*)&forest_size, sizeof(forest_size) );
	fout.write( (const char*)&forest_leaf, sizeof(forest_leaf) );
	fout.write( (const char*)&forest_data_bag, sizeof(forest_data_bag) ); // Not needed for inference but included for completness
	fout.write( (const char*)&forest_feature_bag, sizeof(forest_feature_bag) ); // Not needed for inference but included for completness

	for(size_t i = 0;i < forest_size;++i){
		
		const Tree &t = forest[i];

		size_t num_node = t.size();

		fout.write( (const char*)&num_node, sizeof(size_t) );

		for(size_t j = 0;j < num_node;++j){

			fout.write( (const char*)&(t[j].boundary.first), sizeof(unsigned int) );
			fout.write( (const char*)&(t[j].boundary.second), sizeof(FeatureType) );
			fout.write( (const char*)&(t[j].left), sizeof(unsigned int) );
			fout.write( (const char*)&(t[j].right), sizeof(unsigned int) );
			fout.write( (const char*)&(t[j].prediction), sizeof(PredictionType) );
		}
	}
}

void RandomForest::load_param(const std::string &m_filename)
{
	ifstream fin( m_filename.c_str(), ios::binary);

	if(!fin){
		cerr << "Unable to open " << m_filename << " for reading model parameters" << endl;
		error(errInternal, -1, __FILE__ ":RandomForest::load_param");
	}

	// Write the forest parameters
	fin.read( (char*)&forest_size, sizeof(forest_size) );
	fin.read( (char*)&forest_leaf, sizeof(forest_leaf) );
	fin.read( (char*)&forest_data_bag, sizeof(forest_data_bag) ); // Not needed for inference but included for completness
	fin.read( (char*)&forest_feature_bag, sizeof(forest_feature_bag) ); // Not needed for inference but included for completness

	forest.resize(forest_size);

	for(size_t i = 0;i < forest_size;++i){
		
		Tree &t = forest[i];

		size_t num_node;

		fin.read( (char*)&num_node, sizeof(size_t) );

		for(size_t j = 0;j < num_node;++j){

			t.push_back( TreeNode() );

			TreeNode &n = t.back();

			fin.read( (char*)&(n.boundary.first), sizeof(unsigned int) );
			fin.read( (char*)&(n.boundary.second), sizeof(FeatureType) );
			fin.read( (char*)&(n.left), sizeof(unsigned int) );
			fin.read( (char*)&(n.right), sizeof(unsigned int) );
			fin.read( (char*)&(n.prediction), sizeof(PredictionType) );
		}
	}
}