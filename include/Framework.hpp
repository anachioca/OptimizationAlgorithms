#ifndef FRAMEWORK_HPP
#define FRAMEWORK_HPP

#include <vector>
#include <memory>
#include <functional>

/**
 * Interface for any optimization solution (provides objective value)
 */
class Solution {
public:
    virtual ~Solution() = default; // virtual destructor for cleanup
    virtual double objectiveValue() const = 0;
    virtual std::unique_ptr<Solution> clone() const = 0;
};

/**
 * Generic neighborhood explorer for local search.
 */
template <typename S>
class Neighborhood {
public:
    virtual ~Neighborhood() = default;
    virtual std::unique_ptr<S> findBetterNeighbor(const S& current) = 0; // takes current solution and returns a better neighbor if one exists, otherwise nullptr
};

/**
 * Generic Algorithm interface.
 */
template <typename S>
class Algorithm {
public:
    virtual ~Algorithm() = default;
    virtual std::unique_ptr<S> solve() = 0;
};

/**
 * Generic Selection Strategy for Greedy.
 */
template <typename E>
class SelectionStrategy {
public:
    virtual ~SelectionStrategy() = default;
    virtual void sort(std::vector<E>& elements) = 0; // Sorts the elements in-place according to the strategy
};

/**
 * Generic Local Search Implementation.
 */
template <typename S>
class LocalSearch : public Algorithm<S> {
private:
    std::unique_ptr<S> currentSolution;
    std::unique_ptr<Neighborhood<S>> neighborhood;
    std::function<void(LocalSearch<S>&)> stepCallback;

public:
    LocalSearch(std::unique_ptr<S> startSolution, std::unique_ptr<Neighborhood<S>> nh)
        : currentSolution(std::move(startSolution)), neighborhood(std::move(nh)) {}

    void setStepCallback(std::function<void(LocalSearch<S>&)> callback) {
        stepCallback = callback;
    }

    bool performStep() {
        if (stepCallback) stepCallback(*this);
        
        auto next = neighborhood->findBetterNeighbor(*currentSolution);
        if (next && next->objectiveValue() < currentSolution->objectiveValue()) {
            currentSolution = std::move(next);
            return true;
        }
        return false;
    }

    const S& getCurrentSolution() const { return *currentSolution; }
    S& getMutableSolution() { return *currentSolution; }
    Neighborhood<S>& getNeighborhood() { return *neighborhood; }

    std::unique_ptr<S> solve() override {
        while (performStep()) {
            // keep stepping
        }
        return std::unique_ptr<S>(static_cast<S*>(currentSolution->clone().release())); 
    }
};

/**
 * Generic Greedy Algorithm.
 */
template <typename E, typename S>
class GreedyAlgorithm : public Algorithm<S> {
private:
    std::vector<E> elements; // List of elements to process (rectangles in this case)
    std::unique_ptr<SelectionStrategy<E>> strategy;
    std::unique_ptr<S> initialSolution;
    typedef void (*PlacementFunc)(S&, E); // Function pointer type for placing an element into the solution
    PlacementFunc place; // this way we keep the Framework generic and can use different placement logic if needed

public:
    GreedyAlgorithm(std::vector<E> elements, 
                   std::unique_ptr<SelectionStrategy<E>> strat,
                   std::unique_ptr<S> emptySolution,
                   PlacementFunc p)
        : elements(std::move(elements)), strategy(std::move(strat)), 
          initialSolution(std::move(emptySolution)), place(p) {}

    std::unique_ptr<S> solve() override {
        strategy->sort(elements);
        auto solution = initialSolution->clone();
        S* sPtr = static_cast<S*>(solution.get());
        for (const auto& e : elements) {
            place(*sPtr, e);
        }
        return std::unique_ptr<S>(static_cast<S*>(solution.release()));
    }
};

#endif // FRAMEWORK_HPP
