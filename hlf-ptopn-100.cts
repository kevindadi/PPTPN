// TPN name=C:/Users/78680/Downloads/hlf-ptopn-100.cts

typedef int place; 


// insert here your type definitions using C-like syntax

// insert here your function definitions 
// using C-like syntax





initially { 

// insert here the state variables declarations 
// and possibly some code to initialize them 
// using C-like syntax
 
  

place  P1=0, P2=0, P3=0, P4=0, P5=0, P6=0, P7=0, P8=0, P9=0, P10=0, P11=1, P12=1; }

 transition [ intermediate {   P11 =  P11  - 1; }]  T1 [100,100]
      when (P11 >= 1)
      {   P11 =  P11  - 1 +  1 , P1 = P1 + 1;  }
 transition [ intermediate {   P1 =  P1  - 1; }]  T2 [5,5]
      when (P2 < 1 and P1 >= 1)
      {   P1 = P1  - 1 , P2 =  P2  +  1;  }
 transition [ intermediate {   P2 =  P2  - 1; }]  T3 [3,3]
      when (P2 >= 1)
      {   P2 = P2  - 1 , P3 = P3 + 1;  }
 transition [ intermediate {   P3 =  P3  - 1 , P8 =  P8  - 1; }]  T4 [0,0]
      when (P8 >= 1 and P3 >= 1)
      {   P3 = P3  - 1 , P8 = P8  - 1 , P4 = P4 + 1;  }
 transition [ intermediate {   P4 =  P4  - 1; }]  T5 [8,8]
      when (P4 >= 1)
      {   P4 = P4  - 1 , P5 = P5 + 1;  }
 transition [ intermediate {   P5 =  P5  - 1; }]  T6 [0,0]
      when (P5 >= 1)
      {   P5 = P5  - 1;  }
 transition [ intermediate {   P10 =  P10  - 1; }]  T7 [0,0]
      when (P10 >= 1)
      {   P10 = P10  - 1;  }
 transition [ intermediate {   P9 =  P9  - 1; }]  T8 [3,3]
      when (P1 < 1 and P2 < 1 and P9 >= 1)
      {   P9 = P9  - 1 , P2 = P2  , P1 = P1  , P10 = P10 + 1;  }
 transition [ intermediate {   P7 =  P7  - 1; }]  T9 [28,28]
      when (P4 < 1 and P7 >= 1)
      {   P7 = P7  - 1 , P4 = P4  , P8 = P8 + 1;  }
 transition [ intermediate {   P6 =  P6  - 1; }]  T10 [8,8]
      when (P7 < 1 and P4 < 1 and P6 >= 1)
      {   P6 = P6  - 1 , P4 = P4  , P7 =  P7  +  1 , P9 = P9 + 1;  }
 transition [ intermediate {   P12 =  P12  - 1; }]  T11 [50,50]
      when (P12 >= 1)
      {   P12 =  P12  - 1 +  1 , P6 = P6 + 1;  }
 transition [ intermediate {   P1 =  P1  - 1; }]  T12 [100,100]
      when (P1 >= 1)
      {   P1 = P1  - 1;  }
 transition [ intermediate {   P2 =  P2  - 1; }]  T13 [100,100]
      when (P2 >= 1)
      {   P2 = P2  - 1;  }
 transition [ intermediate {   P3 =  P3  - 1; }]  T14 [100,100]
      when (P3 >= 1)
      {   P3 = P3  - 1;  }
 transition [ intermediate {   P4 =  P4  - 1; }]  T15 [100,100]
      when (P4 >= 1)
      {   P4 = P4  - 1;  }
 transition [ intermediate {   P8 =  P8  - 1; }]  T16 [100,100]
      when (P8 >= 1)
      {   P8 = P8  - 1;  }
 transition [ intermediate {   P9 =  P9  - 1; }]  T17 [100,100]
      when (P9 >= 1)
      {   P9 = P9  - 1;  }
 transition [ intermediate {   P7 =  P7  - 1; }]  T18 [100,100]
      when (P7 >= 1)
      {   P7 = P7  - 1;  }
 transition [ intermediate {   P6 =  P6  - 1; }]  T19 [100,100]
      when (P6 >= 1)
      {   P6 = P6  - 1;  }


  // insert TCTL formula here : check formula
graph [passed=eq]