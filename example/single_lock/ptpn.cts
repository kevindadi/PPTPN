// TPN name=PTPN

typedef int place; 

initially { 
place P1_0=0, P2_1=0, P3_2=0, P4_3=0, P5_4=0, P6_5=0, P7_6=0, P8_7=0, P9_8=0, P10_9=0, P11_10=0, P12_11=0, P13_12=0, P14_13=0, P15_14=0, P16_15=0, P17_16=0, P18_17=0, P19_18=1, P20_19=1, P21_20=0, P22_21=0, P23_22=4, P24_23=4, P25_24=1, P26_25=1; }

 transition [priority=9999, intermediate { P1_0 = P1_0 - 1 , P23_22 = P23_22 - 1; }]  T1_0 [0,0]
      when (P1_0 >= 1 and P23_22 >= 1)
      { P1_0 = P1_0 - 1 , P2_1 = P2_1 + 1 , P23_22 = P23_22 - 1;  }
 transition [priority=9999, intermediate { P2_1 = P2_1 - 1; }]  T2_1 [0,20]
      when (P2_1 >= 1)
      { P2_1 = P2_1 - 1 , P3_2 = P3_2 + 1;  }
 transition [priority=9999, intermediate { P3_2 = P3_2 - 1 , P25_24 = P25_24 - 1; }]  T3_2 [0,0]
      when (P3_2 >= 1 and P25_24 >= 1)
      { P3_2 = P3_2 - 1 , P4_3 = P4_3 + 1 , P25_24 = P25_24 - 1;  }
 transition [priority=9999, intermediate { P4_3 = P4_3 - 1; }]  T4_3 [20,40]
      when (P4_3 >= 1)
      { P4_3 = P4_3 - 1 , P5_4 = P5_4 + 1 , P25_24 = P25_24 + 1;  }
 transition [priority=9999, intermediate { P5_4 = P5_4 - 1; }]  T5_4 [40,60]
      when (P5_4 >= 1)
      { P5_4 = P5_4 - 1 , P6_5 = P6_5 + 1 , P23_22 = P23_22 + 1;  }
 transition [priority=9899, intermediate { P7_6 = P7_6 - 1 , P24_23 = P24_23 - 1; }]  T6_5 [0,0]
      when (P7_6 >= 1 and P24_23 >= 1)
      { P7_6 = P7_6 - 1 , P8_7 = P8_7 + 1 , P24_23 = P24_23 - 1;  }
 transition [priority=9899, intermediate { P8_7 = P8_7 - 1; }]  T7_6 [0,5]
      when (P8_7 >= 1)
      { P8_7 = P8_7 - 1 , P9_8 = P9_8 + 1;  }
 transition [priority=9899, intermediate { P9_8 = P9_8 - 1 , P26_25 = P26_25 - 1; }]  T8_7 [0,0]
      when (P9_8 >= 1 and P26_25 >= 1)
      { P9_8 = P9_8 - 1 , P10_9 = P10_9 + 1 , P26_25 = P26_25 - 1;  }
 transition [priority=9899, intermediate { P10_9 = P10_9 - 1; }]  T9_8 [5,10]
      when (P10_9 >= 1)
      { P10_9 = P10_9 - 1 , P11_10 = P11_10 + 1 , P26_25 = P26_25 + 1;  }
 transition [priority=9899, intermediate { P11_10 = P11_10 - 1; }]  T10_9 [10,15]
      when (P11_10 >= 1)
      { P11_10 = P11_10 - 1 , P12_11 = P12_11 + 1 , P24_23 = P24_23 + 1;  }
 transition [priority=9799, intermediate { P13_12 = P13_12 - 1 , P23_22 = P23_22 - 1; }]  T11_10 [0,0]
      when (P13_12 >= 1 and P23_22 >= 1)
      { P13_12 = P13_12 - 1 , P14_13 = P14_13 + 1 , P23_22 = P23_22 - 1;  }
 transition [priority=9799, intermediate { P14_13 = P14_13 - 1; }]  T12_11 [0,10]
      when (P14_13 >= 1)
      { P14_13 = P14_13 - 1 , P15_14 = P15_14 + 1;  }
 transition [priority=9799, intermediate { P15_14 = P15_14 - 1 , P25_24 = P25_24 - 1; }]  T13_12 [0,0]
      when (P15_14 >= 1 and P25_24 >= 1)
      { P15_14 = P15_14 - 1 , P16_15 = P16_15 + 1 , P25_24 = P25_24 - 1;  }
 transition [priority=9799, intermediate { P16_15 = P16_15 - 1; }]  T14_13 [10,30]
      when (P16_15 >= 1)
      { P16_15 = P16_15 - 1 , P17_16 = P17_16 + 1 , P25_24 = P25_24 + 1;  }
 transition [priority=9799, intermediate { P17_16 = P17_16 - 1; }]  T15_14 [30,50]
      when (P17_16 >= 1)
      { P17_16 = P17_16 - 1 , P18_17 = P18_17 + 1 , P23_22 = P23_22 + 1;  }
 transition [priority=0, intermediate { P18_17 = P18_17 - 1; }]  T16_15 [0,0]
      when (P18_17 >= 1)
      { P7_6 = P7_6 + 1 , P18_17 = P18_17 - 1;  }
 transition [priority=0, intermediate { P12_11 = P12_11 - 1; }]  T17_16 [0,0]
      when (P12_11 >= 1)
      { P1_0 = P1_0 + 1 , P12_11 = P12_11 - 1;  }
 transition [priority=0, intermediate { P19_18 = P19_18 - 1; }]  T18_17 [100,100]
      when (P19_18 >= 1)
      { P13_12 = P13_12 + 1 , P19_18 = P19_18 - 1 + 1;  }
 transition [priority=0, intermediate { P20_19 = P20_19 - 1; }]  T19_18 [200,200]
      when (P20_19 >= 1)
      { P1_0 = P1_0 + 1 , P20_19 = P20_19 - 1 + 1;  }
 transition [priority=0, intermediate { P6_5 = P6_5 - 1; }]  T20_19 [0,0]
      when (P6_5 >= 1)
      { P6_5 = P6_5 - 1;  }
 transition [priority=9998, intermediate { P1_0 = P1_0 - 1 , P14_13 = P14_13 - 1; }]  T21_20 [0,0]
      when (P1_0 >= 1 and P14_13 >= 1)
      { P1_0 = P1_0 - 1 , P2_1 = P2_1 + 1 , P14_13 = P14_13 - 1 , P21_20 = P21_20 + 1;  }
 transition [priority=0, intermediate { P3_2 = P3_2 - 1 , P21_20 = P21_20 - 1; }]  T22_21 [0,0]
      when (P3_2 >= 1 and P21_20 >= 1)
      { P3_2 = P3_2 - 1 , P14_13 = P14_13 + 1 , P21_20 = P21_20 - 1;  }
 transition [priority=9998, intermediate { P1_0 = P1_0 - 1 , P16_15 = P16_15 - 1; }]  T23_22 [0,0]
      when (P1_0 >= 1 and P16_15 >= 1)
      { P1_0 = P1_0 - 1 , P2_1 = P2_1 + 1 , P16_15 = P16_15 - 1 , P22_21 = P22_21 + 1;  }
 transition [priority=0, intermediate { P3_2 = P3_2 - 1 , P22_21 = P22_21 - 1; }]  T24_23 [0,0]
      when (P3_2 >= 1 and P22_21 >= 1)
      { P3_2 = P3_2 - 1 , P16_15 = P16_15 + 1 , P22_21 = P22_21 - 1;  }

graph [passed=eq]
