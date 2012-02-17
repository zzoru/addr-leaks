#include <iostream>

#include "PointerAnalysis.h"

// ============================================= //

///  Default constructor
PointerAnalysis::PointerAnalysis()
{
}

// ============================================= //

/// Destructor
PointerAnalysis::~PointerAnalysis()
{
}

// ============================================= //

/**
 * Add a constraint of type: A = &B
 */
void PointerAnalysis::addAddr(int A, int B)
{
	// Ensure nodes A and B exists.
	addNode(A);
	addNode(B);
	
	// Add B to pts(A)
	addToPts(B, A);
}

// ============================================= //

/**
 * Add a constraint of type: A = B
 */
void PointerAnalysis::addBase(int A, int B)
{
	// Ensure nodes A and B exists.
	addNode(A);
	addNode(B);
	
	// Add edge from B to A
	addEdge(B, A);	
}

// ============================================= //

/**
 * Add a constraint of type: *A = B
 */
void PointerAnalysis::addStore(int A, int B)
{
	// Ensure nodes A and B exists.
	addNode(A);
	addNode(B);
	
	// Add the constraint
	stores[A].insert(B);
}

// ============================================= //

/**
 * Add a constraint of type: A = *B
 */
void PointerAnalysis::addLoad(int A, int B)
{
	// Ensure nodes A and B exists.
	addNode(A);
	addNode(B);
	
	// Add the constraint
	loads[B].insert(A);
}

// ============================================= //

/**
 * Return the set of positions pointed by A:
 *   pointsTo(A) = {B1, B2, ...}
 *  TODO: Check this
 */
std::set<int> PointerAnalysis::pointsTo(int A)
{
	int repA = vertices[A];
	return pointsToSet[A];
}

// ============================================= //

/**
 * Add a new node to the graph if it doesn't already exist.
 */
void PointerAnalysis::addNode(int id)
{
	// Only add the node if it doesn't exist
	if (vertices.find(id) == vertices.end()) 
	{
		// Its current representative is itself and it is active
		vertices[id] = id;
		activeVertices.insert(id);
	}
}

// ============================================= //

/**
 * Add an edge in the graph.
 */
void PointerAnalysis::addEdge(int fromId, int toId)
{
	// We work with the representatives, so get them first.
	int repFrom = fromId;//vertices[fromId];
	int repTo = toId;//vertices[toId];
	
	// Add the edge (both directions)
	from[repFrom].insert(repTo);
    to[repTo].insert(repFrom);
}

// ============================================= //

/**
 * Add a node to the points-to set of another node
 */
void PointerAnalysis::addToPts(int pointed, int pointee)
{
	// Add the reference
	pointsToSet[pointee].insert(pointed);
}

// ============================================= //

void PointerAnalysis::removeCycles()
{
    // Some needed variables
    IntMap order;
    IntMap repr;
    int i = 0;
    IntSet current;
    IntDeque S;
    
    // At first, no node is visited and their representatives are themselves
    std::cout << "Initializing... " << std::endl;
    IntSet::iterator V;
    for (V = activeVertices.begin(); V != activeVertices.end(); V++) 
	{
        order[*V] = 0;
        repr[*V] = *V;
    }

    // Visit all unvisited nodes
    std::cout << "Visiting... " << std::endl;
    for (V = activeVertices.begin(); V != activeVertices.end(); V++) 
	{
        if (order[*V] == 0) 
		{
            visit(*V, order, repr, i, current, S);
        }
    }
    
    // Merge those whose representatives are not themselves
    std::cout << "Merging... " << std::endl;
    for (V = activeVertices.begin(); V != activeVertices.end(); V++) 
	{
        std::cout << "Current vertice: " << *V << std::endl;
        if (repr[*V] != *V) 
		{
            merge(*V, repr[*V]);
        }
    }
}

// ============================================= //

void PointerAnalysis::visit(int Node, IntMap& Order, IntMap& Repr, 
	int& idxOrder, IntSet& Curr, IntDeque& Stack)
{
	idxOrder++;
    Order[Node] = idxOrder;

    IntSet::iterator w;
    for (w = from[Node].begin(); w != from[Node].end(); w++) 
	{
        if (Order[*w] == 0) visit(*w, Order, Repr, idxOrder, Curr, Stack);
        if (Curr.find(*w) == Curr.end()) 
		{
            Repr[Node] = (Order[Repr[Node]] < Order[Repr[*w]]) ? 
                Repr[Node] : 
                Repr[*w]
            ;
        }
    }

    if (Repr[Node] == Node) 
	{
        Curr.insert(Node);
        while (!Stack.empty()) 
		{
            int w = Stack.front();
            if (Order[w] <= Order[Node]) break;
            else 
			{
                Stack.pop_front();
                Curr.insert(w);
                Repr[w] = Node;
            }
        }
        // Push(TopologicalOrder, Node)
    }
    else 
	{
        Stack.push_front(Node);
    }
}
	
// ============================================= //

/**
 * Merge two nodes.
 * @param id the noded being merged
 * @param target the noded to merge into
 */
void PointerAnalysis::merge(int id, int target)
{
    std::cout << "Merge " << id << " into " << target << std::endl;
	// Remove all edges id->target, target->id
    from[id].erase(target);
    to[target].erase(id);
    from[target].erase(id);
    to[id].erase(target);

    // Move all edges id->v to target->v
    std::cout << "Outgoing edges..." << std::endl;
    IntSet::iterator v;
    for (v = from[id].begin(); v != from[id].end(); v++) 
	{
        from[target].insert(*v);
        to[*v].erase(id);
        to[*v].insert(target);
    }
         
    // Move all edges v->id to v->target
    std::cout << "Incoming edges..." << std::endl;
    for (v = to[id].begin(); v != to[id].end(); v++) 
	{
        to[target].insert(*v);
        from[*v].erase(id);
        from[*v].insert(target);
    }

    // Mark the representative vertex
    vertices[id] = target;
    std::cout << "Removing vertice " << id << " from active." << std::endl;
	activeVertices.erase(id);

    // Merge Stores
    std::cout << "Stores..." << std::endl;
    for (v = stores[id].begin(); v != stores[id].end(); v++) 
	{
        stores[target].insert(*v);
    }
    stores[id].clear(); // Not really needed, I think

    // Merge Loads
    std::cout << "Loads..." << std::endl;
    for (v = loads[id].begin(); v != loads[id].end(); v++) 
	{
        loads[target].insert(*v);
    }
    loads[id].clear();

    // Join Points-To set
    std::cout << "Points-to-set..." << std::endl;
    for (v = pointsToSet[id].begin(); v != pointsToSet[id].end(); v++) 
	{
        pointsToSet[target].insert(*v);
    }
    std::cout << "End of merging..." << std::endl;
}

// ============================================= //

/**
 * Execute the pointer analysis
 * TODO: Add info about the analysis
 */
void PointerAnalysis::solve()
{
    std::set<std::string> R;
    IntSet WorkSet = activeVertices;
    IntSet NewWorkSet;
        
    std::cout << "Starting..." << std::endl;

    while (!WorkSet.empty()) {
        int Node = *WorkSet.begin();
        WorkSet.erase(WorkSet.begin());
        
        std::cout << "New Step" << std::endl;
        std::cout << "Current Node: " << Node << std::endl;    

        // For V in pts(Node)
        IntSet::iterator V;
        for (V = pointsToSet[Node].begin(); V != pointsToSet[Node].end(); V++ ) 
		{
            std::cout << "Current V: " << *V << std::endl;
            std::cout << "Load Constraints" << std::endl;
            // For every constraint A = *Node
            IntSet::iterator A;
            for (A=loads[Node].begin(); A != loads[Node].end(); A++) 
            {
                // If V->A not in Graph
                if (from[*V].find(*A) == from[*V].end()) 
				{
                    addEdge(*A, *V);
                    NewWorkSet.insert(*V);
                }
            }

            std::cout << "Store Constraints" << std::endl;
            // For every constraint *Node = B
            IntSet::iterator B;
            for (B=stores[Node].begin(); B != stores[Node].end(); B++) 
            {
                // If B->V not in Graph
                if (from[*B].find(*V) == from[*B].end()) 
				{
                    addEdge(*V, *B);
                    NewWorkSet.insert(*B);
                }
            }
        }
		
        std::cout << "End step" << std::endl;
        // For Node->Z in Graph
        IntSet::iterator Z = from[Node].begin();
        while (Z != from[Node].end() ) 
		{
            IntSet::iterator NextZ = Z;
            NextZ++;
            int ZVal = *Z;
            std::string edge = "" + Node;
            edge += "->";
            edge += ZVal;

            // Compare points-to sets
            if (pointsToSet[ZVal] == pointsToSet[Node] && R.find(edge) == R.end() ) 
			{
                std::cout << "Removing cycles..." << std::endl;
                removeCycles();
                R.insert(edge);
                std::cout << "Cycles removed" << std::endl;
            }

            // Merge the points-To Set
            bool changed = false;
            for (V = pointsToSet[Node].begin(); V != pointsToSet[Node].end(); V++) 
			{
                changed |= pointsToSet[ZVal].insert(*V).second;
            }

            // Add Z to WorkSet if pointsToSet(Z) changed
            if (changed) 
			{
                NewWorkSet.insert(ZVal);
            }
    
            Z = NextZ;
        }
        
        // Swap WorkSets if needed
        if (WorkSet.empty()) WorkSet.swap(NewWorkSet);
    }
}

// ============================================= //

/// Prints the graph to std output
void PointerAnalysis::print()
{

    std::cout << vertices.size() << std::endl; 
    IntSet::iterator it;
    IntMap::iterator v;
	
    // Print Vertices Representatives
    for (v = vertices.begin(); v != vertices.end(); v++) 
	{
        std::cout << v->first << " -> ";
        std::cout << v->second << std::endl;
    }
    std::cout << std::endl;

    // Print Vertices Connections
    for (it = activeVertices.begin(); it != activeVertices.end(); it++) 
	{
        std::cout << *it << " -> ";
        IntSet::iterator n;
        for (n = from[*it].begin(); n!= from[*it].end(); n++) 
        {
            std::cout << *n << " ";
        }
        //std::cout << "0";
        std::cout << std::endl;
    }
    std::cout << std::endl;
    
    // Prints PointsTo sets
    for (it = activeVertices.begin(); it != activeVertices.end(); it++) 
	{
        std::cout << *it << " -> {";
        IntSet::iterator n;
        for (n = pointsToSet[*it].begin(); n != pointsToSet[*it].end();  n++) 
        {
            std::cout << *n << ", ";
        }

        std::cout << "}";
        std::cout << std::endl;
    }
    std::cout << std::endl;

}

// ============================================= //

/// Returns the points-to map
std::map<int, std::set<int> > PointerAnalysis::allPointsTo() {
    return pointsToSet;
}

