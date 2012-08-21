//   Leonardo Vilela Teixeira

#ifndef POINTER_ANALYSIS_H
#define POINTER_ANALYSIS_H

#include <set>
#include <map>
#include <deque>

// ============================================= //

typedef std::set<int> IntSet;
typedef std::map<int, IntSet> IntSetMap;
typedef std::map<int, int> IntMap;
typedef std::deque<int> IntDeque;

// ============================================= //

class PointerAnalysis {
    
    public:
        PointerAnalysis();
        ~PointerAnalysis();

        // Add a constraint of type: A = &B
        void addAddr(int A, int B);

        // Add a constraint of type: A = B
        void addBase(int A, int B);

        // Add a constraint of type: *A = B
        void addStore(int A, int B);

        // Add a constraint of type: A = *B
        void addLoad(int A, int B);

        // Execute the pointer analysis
        void solve();

        // Return the set of positions pointed by A:
        //   pointsTo(A) = {B1, B2, ...}
        std::set<int>  pointsTo(int A);
	
        // Return the points-to map
        std::map<int, std::set<int> > allPointsTo();

		void print();
	
	private:
		void addNode(int id);
		void addEdge(int fromId, int toId);
		void addToPts(int pointed, int pointee);
		void merge(int id, int target);
		void visit(int Node, IntMap& Order, IntMap& Repr, int& idxOrder,
			IntSet& Curr, IntDeque& Stack);
        void removeCycles();
	
		// Hold the points-to Set
		IntSetMap pointsToSet;

		// Hold the vertices and their representatives
        IntMap vertices;
		
		// Hold the active vertices
		IntSet activeVertices;
		
		// Hold the graph structure
        IntSetMap from;
        IntSetMap to;

		// Hold the complex constraints
        IntSetMap loads;
        IntSetMap stores;
};

// ============================================= //

#endif  /* POINTER_ANALYSIS_H */
