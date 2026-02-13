#!/bin/bash
#
# Enhanced PGR script with timing measurements
# Generates PGR files and runs planner while recording execution times
#
# Usage: pgr_with_timing.sh domain.hddl problem.hddl planfile.txt output_dir
#

if [[ $# -ne 4 ]]
then
  echo "usage: pgr_with_timing.sh domain.hddl problem.hddl planfile.txt output_dir"
  exit 1
fi

DOMAIN=$1
PROBLEM=$2
PLANFILE=$3
OUTPUT_DIR=$4

mkdir -p "$OUTPUT_DIR"

# Initialize timing CSV
TIMING_FILE="$OUTPUT_DIR/planner_timing.csv"
echo "observation_prefix,pgr_file,planner_time_seconds,exit_code,status" > "$TIMING_FILE"

./pandaPIparser "$DOMAIN" "$PROBLEM" temp.parsed
if [ ! -f temp.parsed ]; then
  echo "Parsing failed."
  exit 101
fi

PSAS_FILE="$(basename "$DOMAIN" .hddl)-$(basename "$PROBLEM" .hddl).psas"
./pandaPIgrounder -q temp.parsed "$PSAS_FILE"
rm temp.parsed
if [ ! -f "$PSAS_FILE" ]; then
  echo "Grounding failed."
  exit 10
fi

# Move PSAS file to output directory
mv "$PSAS_FILE" "$OUTPUT_DIR/"
PSAS_FILE="$OUTPUT_DIR/$PSAS_FILE"

# Determine number of plan steps in the solution
steps=$(grep -o "([^()]*)" "$PLANFILE" | wc -l)

echo "Processing $steps observation prefixes..."

for i in $(seq 0 $steps)
do
  ./htnPrefixEncoding "pgrfo" "$PSAS_FILE" "$PLANFILE" "$i"
  
  if (( i < steps )); then
    if (( i < 10 )); then
      pgrprob="$PLANFILE-00$i.pgr"
    elif (( i < 100 )); then
      pgrprob="$PLANFILE-0$i.pgr"
    else
      pgrprob="$PLANFILE-$i.pgr"
    fi
  else
    pgrprob="$PLANFILE-full.pgr"
  fi
  
  # Move PGR file to output directory
  if [ -f "$pgrprob" ]; then
    mv "$pgrprob" "$OUTPUT_DIR/"
    pgrprob="$OUTPUT_DIR/$(basename "$pgrprob")"
  else
    echo "ERROR: PGR file not created: $pgrprob"
    echo "$i,$pgrprob,0,ERROR,pgr_not_created" >> "$TIMING_FILE"
    continue
  fi
  
  echo "Running planner for observation prefix $i..."
  
  # Time the planner execution
  START_TIME=$(date +%s.%N)
  
  ./pplanner "$pgrprob" > "${pgrprob}.log" 2>&1
  EXIT_CODE=$?
  
  END_TIME=$(date +%s.%N)
  
  # Calculate execution time
  EXEC_TIME=$(echo "$END_TIME - $START_TIME" | bc)
  
  # Determine status
  if [ $EXIT_CODE -eq 0 ]; then
    STATUS="success"
  else
    STATUS="failed"
  fi
  
  # Check for unsolvable
  if grep -qi "unsolvable\|no solution" "${pgrprob}.log"; then
    STATUS="unsolvable"
  fi
  
  echo "$i,$(basename "$pgrprob"),$EXEC_TIME,$EXIT_CODE,$STATUS" >> "$TIMING_FILE"
  
  # Create human-readable plan
  ./pandaPIparser -c "${pgrprob}.log" "${pgrprob}.plan" 2>/dev/null
  
  echo "  Completed in ${EXEC_TIME}s (status: $STATUS)"
done

echo "All PGR processing complete. Timing data saved to $TIMING_FILE"
exit 0
