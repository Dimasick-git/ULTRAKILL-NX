#pragma once
struct NxFgBudget {
 unsigned consecutiveSlow=0;
 bool observed=false;
 // FG must leave time for the real game frame within a 33.3ms pair budget.
 bool Observe(double milliseconds,bool reset){
  (void)reset;
  if(!observed){observed=true;return false;}
  consecutiveSlow=milliseconds>16.667?consecutiveSlow+1:0;
  return consecutiveSlow>=2;
 }
};
