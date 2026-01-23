#ifndef __SYMMETRIC_MATRIX
#define __SYMMETRIC_MATRIX

template<class T>
class SymmetricMatrix : private std::vector<T>
{
	private:

		size_t len; // Number of rows = number of columns

	public:

		typedef typename std::vector<T>::iterator iterator;
		typedef typename std::vector<T>::const_iterator const_iterator;

		SymmetricMatrix(const size_t &m_len = 0, const T &m_init = T())
		{
			resize(m_len, m_init);
		};

		inline iterator begin()
		{
			return std::vector<T>::begin();
		};

		inline const_iterator begin() const
		{
			return std::vector<T>::begin();
		};

		inline iterator end()
		{
			return std::vector<T>::end();
		};

		inline const_iterator end() const
		{
			return std::vector<T>::end();
		};

		inline void resize(const size_t &m_len, const T &m_init = T())
		{
			len = m_len;

			// The size of a symmetrix matrix is N*(N + 1)/2
			std::vector<T>::resize(size(), m_init);
		};

		inline void zero()
		{
			for(typename std::vector<T>::iterator i = std::vector<T>::begin();i != std::vector<T>::end();++i){
				*i = 0;
			}
		};

		// For a symmetric matrix, num_row == num_col
		inline size_t num_row() const
		{
			return len;
		};

		inline size_t num_col() const
		{
			return len;
		};

		inline size_t size() const
		{
			return len*(len + 1)/2;
		};

		inline T& operator()(size_t m_row /*copy*/, size_t m_col /*copy*/)
		{
			const size_t max_index = std::max(m_row, m_col);
			const size_t min_index = std::min(m_row, m_col);

			if(max_index >= len){
				throw __FILE__ ":SymmetricMatrix::operator(): Index out of bounds";
			}

			return std::vector<T>::operator[](min_index*len - min_index*(min_index + 1)/2 + max_index);
		};

		inline T operator()(size_t m_row /*copy*/, size_t m_col /*copy*/) const
		{
			const size_t max_index = std::max(m_row, m_col);
			const size_t min_index = std::min(m_row, m_col);

			if(max_index >= len){
				throw __FILE__ ":SymmetricMatrix::operator() const: Index out of bounds";
			}

			return std::vector<T>::operator[](min_index*len - min_index*(min_index + 1)/2 + max_index);
		};

		inline SymmetricMatrix& operator+=(const SymmetricMatrix &m_rhs)
		{
			if(len != m_rhs.len){
				throw __FILE__ ":SymmetricMatrix::operator+=: Cannot combine matricies with unequal sizes";
			}

			const_iterator j = m_rhs.begin();

			for(iterator i = begin();i != end();++i, ++j){
				*i += *j;
			}

			return *this;
		};

		inline SymmetricMatrix& operator-=(const SymmetricMatrix &m_rhs)
		{
			if(len != m_rhs.len){
				throw __FILE__ ":SymmetricMatrix::operator-=: Cannot combine matricies with unequal sizes";
			}

			const_iterator j = m_rhs.begin();

			for(iterator i = begin();i != end();++i, ++j){
				*i -= *j;
			}

			return *this;
		};

		inline SymmetricMatrix& operator*=(const SymmetricMatrix &m_rhs)
		{
			if(len != m_rhs.len){
				throw __FILE__ ":SymmetricMatrix::operator*=: Cannot combine matricies with unequal sizes";
			}

			const_iterator j = m_rhs.begin();

			for(iterator i = begin();i != end();++i, ++j){
				*i *= *j;
			}

			return *this;
		};
};

// Compute the row and column for a given index and matrix size
// From: https://stackoverflow.com/questions/27086195/linear-index-upper-triangular-matrix
inline std::pair<size_t, size_t> row_column(const size_t &m_index, const size_t &m_len)
{
	const size_t row = m_len - 1 - floor(0.5*(sqrt( pow(2*m_len + 1, 2) - 8*(m_index + 1) ) - 1));
	const size_t col = m_index - m_len*row + (row*(row + 1))/2;
	
	return std::make_pair(row, col);
};

#endif // __SYMMETRIC_MATRIX