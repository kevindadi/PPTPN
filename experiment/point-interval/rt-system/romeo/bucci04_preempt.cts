// TPN name=C:/Users/78680/Downloads/romeo-3.10.11-win64/romeo-3.10.11/examples/bucci04_preempt.cts

typedef int place; 


// insert here your type definitions using C-like syntax


// insert here your function definitions 
// using C-like syntax

 

initially { 
 // insert here the state variables declarations 
// and possibly some code to initialize them 
// using C-like syntax 
  

place  Ready1=1, Ready2=1, Ending3=0, Ending1=0, Ready3=1, Ending2=0; }

 transition Act1 [50,50]
      when (true)
      {   Ready1 = Ready1 + 1;  }
 transition [ intermediate {   Ready1 =  Ready1  - 1; }]  Exec1 [10,20]
      when (Ready1 >= 1)
      {   Ready1 = Ready1  - 1 , Ending1 = Ending1 + 1;  }
 transition Act2 [100,inf]
      when (true)
      {   Ready2 = Ready2 + 1;  }
 transition Act3 [150,150]
      when (true)
      {   Ready3 = Ready3 + 1;  }
 transition [ intermediate {   Ready3 =  Ready3  - 1; }, speed= min(min(1,max(0,1-Ready1)),max(0,1-Ready2)) ]  Exec3 [20,28]
      when (Ready3 >= 1)
      {   Ready3 = Ready3  - 1 , Ready1 = Ready1  , Ready2 = Ready2  , Ending3 = Ending3 + 1;  }
 transition [ intermediate {   Ready2 =  Ready2  - 1; }, speed= min(1,max(0,1-Ready1)) ]  Exec2 [18,28]
      when (Ready2 >= 1)
      {   Ready2 = Ready2  - 1 , Ready1 = Ready1  , Ending2 = Ending2 + 1;  }
 transition [ intermediate {   Ending3 =  Ending3  - 1; }]  end3 [0,0]
      when (Ending3 >= 1)
      {   Ending3 = Ending3  - 1;  }
 transition [ intermediate {   Ending1 =  Ending1  - 1; }]  end1 [0,0]
      when (Ending1 >= 1)
      {   Ending1 = Ending1  - 1;  }
 transition [ intermediate {   Ending2 =  Ending2  - 1; }]  end2 [0,0]
      when (Ending2 >= 1)
      {   Ending2 = Ending2  - 1;  }


  // insert TCTL formula here : check formula
graph [passed=eq]