#!/bin/bash

# Get absolute path to current directory
SCRIPT_DIR="$(cd "$(dirname "${BAsSH_SOURCE[0]}")" && pwd)"

# Number of parallel jobs per problem (default: 4, adjust based on your CPU cores)
MAX_JOBS=${MAX_JOBS:-10}

# Create results directory
mkdir -p results_full

# Function to process a single observation count
process_observation() {
    local BASE_NAME=$1
    local OBS_COUNT=$2
    local PROBLEM_DIR=$3
    local NUM_ACTIONS=$4
    
    echo "  [Job $OBS_COUNT/$NUM_ACTIONS] Running with $OBS_COUNT observations"
    
    # Create timing file
    OBS_DIR="$PROBLEM_DIR/obs_$OBS_COUNT"
    mkdir -p "$OBS_DIR"
    
    # Start timing
    START_TIME=$(date +%s.%N)
    
    # Create unique work directory for this observation count
    WORK_DIR="$OBS_DIR/posterior_estimation"
    
    # Run compute_posterior.sh in current directory with explicit work dir
    ./compute_posterior.sh \
        benchmarks/kitchen-100/00-domain/domain_explicit_hypotheses.hddl \
        benchmarks/kitchen-100/01-problems/$BASE_NAME.hddl \
        benchmarks/kitchen-100/02-solutions/$BASE_NAME.txt \
        $OBS_COUNT 5 "$WORK_DIR" > "$OBS_DIR/run.log" 2>&1
    
    # End timing
    END_TIME=$(date +%s.%N)
    ELAPSED=$(echo "$END_TIME - $START_TIME" | bc)
    
    # Log timing information
    echo "Elapsed time: $ELAPSED seconds" > "$OBS_DIR/timing.txt"
    echo "Start: $(date -r ${START_TIME%.*} '+%Y-%m-%d %H:%M:%S')" >> "$OBS_DIR/timing.txt"
    echo "End: $(date -r ${END_TIME%.*} '+%Y-%m-%d %H:%M:%S')" >> "$OBS_DIR/timing.txt"
    
    # Check if posterior estimation completed successfully
    if [ -d "$WORK_DIR" ] && [ -f "$WORK_DIR/posterior_results.txt" ]; then
        echo "    [Job $OBS_COUNT/$NUM_ACTIONS] -> Results saved to $OBS_DIR (${ELAPSED}s)"
    else
        echo "    [Job $OBS_COUNT/$NUM_ACTIONS] -> Warning: Computation may have failed (${ELAPSED}s)"
    fi
}

export -f process_observation
export SCRIPT_DIR

# for each problem file in benchmarks/kitchen-100/01-problems
for PROBLEM_FILE in benchmarks/kitchen-100/01-problems/p-*.hddl; do
    # extract base name without path and extension
    BASE_NAME=$(basename "$PROBLEM_FILE" .hddl)
    
    # Create directory for this problem
    PROBLEM_DIR="results_full/$BASE_NAME"
    mkdir -p "$PROBLEM_DIR"

    echo "Running PGR solving for problem: $BASE_NAME"
    OBSERVATION_FILE="benchmarks/kitchen-100/02-solutions/$BASE_NAME.txt"

    # Get number of actions in the solution
    NUM_ACTIONS=$(grep -o "([^()]*)" "$OBSERVATION_FILE" | wc -l | tr -d ' ')
    echo "  Number of actions in solution: $NUM_ACTIONS"
    echo "  Using $MAX_JOBS parallel jobs"
    
    # Start timing for entire problem
    PROBLEM_START=$(date +%s.%N)
    
    # Use GNU parallel or xargs for parallel execution
    if command -v parallel &> /dev/null; then
        # Use GNU parallel if available
        seq 0 $NUM_ACTIONS | parallel -j $MAX_JOBS process_observation $BASE_NAME {} "$PROBLEM_DIR" $NUM_ACTIONS
    else
        # Fallback to xargs with background jobs
        seq 0 $NUM_ACTIONS | xargs -n 1 -P $MAX_JOBS -I {} bash -c "process_observation $BASE_NAME {} '$PROBLEM_DIR' $NUM_ACTIONS"
    fi
    
    # End timing for entire problem
    PROBLEM_END=$(date +%s.%N)
    PROBLEM_ELAPSED=$(echo "$PROBLEM_END - $PROBLEM_START" | bc)
    
    # Log overall problem timing
    echo "Total time for $BASE_NAME: ${PROBLEM_ELAPSED}s" | tee "$PROBLEM_DIR/total_timing.txt"
    echo ""

    # Clean up any temporary files if needed
done


