// TPN name=PTPN

typedef int place; 

initially { 
place P1_0=1, P2_1=0, P3_2=0, P4_3=0, P5_4=0, P6_5=0, P7_6=1, P8_7=0, P9_8=0, P10_9=0, P11_10=0, P12_11=0, P13_12=1, P14_13=1, P15_14=1; }

 transition [priority=2099, intermediate { P1_0 = P1_0 - 1 , P14_13 = P14_13 - 1; }]  T1_0 [0,0]
      when (P1_0 >= 1 and P14_13 >= 1)
      { P1_0 = P1_0 - 1 , P2_1 = P2_1 + 1 , P14_13 = P14_13 - 1;  }
 transition [priority=2099, intermediate { P2_1 = P2_1 - 1; }]  T2_1 [1,1]
      when (P2_1 >= 1)
      { P2_1 = P2_1 - 1 , P3_2 = P3_2 + 1;  }
 transition [priority=2099, intermediate { P3_2 = P3_2 - 1 , P15_14 = P15_14 - 1; }]  T3_2 [0,0]
      when (P3_2 >= 1 and P15_14 >= 1)
      { P3_2 = P3_2 - 1 , P4_3 = P4_3 + 1 , P15_14 = P15_14 - 1;  }
 transition [priority=2099, intermediate { P4_3 = P4_3 - 1; }]  T4_3 [2,4]
      when (P4_3 >= 1)
      { P4_3 = P4_3 - 1 , P5_4 = P5_4 + 1 , P15_14 = P15_14 + 1;  }
 transition [priority=2099, intermediate { P5_4 = P5_4 - 1; }]  T5_4 [1,2]
      when (P5_4 >= 1)
      { P5_4 = P5_4 - 1 , P6_5 = P6_5 + 1 , P14_13 = P14_13 + 1;  }
 transition [priority=1099, intermediate { P7_6 = P7_6 - 1 , P13_12 = P13_12 - 1; }]  T6_5 [0,0]
      when (P7_6 >= 1 and P13_12 >= 1)
      { P7_6 = P7_6 - 1 , P8_7 = P8_7 + 1 , P13_12 = P13_12 - 1;  }
 transition [priority=1099, intermediate { P8_7 = P8_7 - 1; }]  T7_6 [1,2]
      when (P8_7 >= 1)
      { P8_7 = P8_7 - 1 , P9_8 = P9_8 + 1;  }
 transition [priority=1099, intermediate { P9_8 = P9_8 - 1 , P15_14 = P15_14 - 1; }]  T8_7 [0,0]
      when (P9_8 >= 1 and P15_14 >= 1)
      { P9_8 = P9_8 - 1 , P10_9 = P10_9 + 1 , P15_14 = P15_14 - 1;  }
 transition [priority=1099, intermediate { P10_9 = P10_9 - 1; }]  T9_8 [3,5]
      when (P10_9 >= 1)
      { P10_9 = P10_9 - 1 , P11_10 = P11_10 + 1 , P15_14 = P15_14 + 1;  }
 transition [priority=1099, intermediate { P11_10 = P11_10 - 1; }]  T10_9 [1,1]
      when (P11_10 >= 1)
      { P11_10 = P11_10 - 1 , P12_11 = P12_11 + 1 , P13_12 = P13_12 + 1;  }
 transition [priority=0, intermediate { P12_11 = P12_11 - 1; }]  T11_10 [0,0]
      when (P12_11 >= 1)
      { P12_11 = P12_11 - 1;  }
 transition [priority=0, intermediate { P6_5 = P6_5 - 1; }]  T12_11 [0,0]
      when (P6_5 >= 1)
      { P6_5 = P6_5 - 1;  }

graph [passed=eq]
