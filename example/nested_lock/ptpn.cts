// TPN name=PTPN

typedef int place; 

initially { 
place P1_0=0, P2_1=0, P3_2=0, P4_3=0, P5_4=0, P6_5=0, P7_6=0, P8_7=0, P9_8=0, P10_9=0, P11_10=0, P12_11=0, P13_12=0, P14_13=0, P15_14=0, P16_15=0, P17_16=0, P18_17=0, P19_18=0, P20_19=0, P21_20=0, P22_21=0, P23_22=0, P24_23=0, P25_24=0, P26_25=0, P27_26=0, P28_27=1, P29_28=1, P30_29=0, P31_30=0, P32_31=4, P33_32=4, P34_33=1, P35_34=1, P36_35=1; }

 transition [priority=9999, intermediate { P1_0 = P1_0 - 1 , P32_31 = P32_31 - 1; }]  T1_0 [0,0]
      when (P1_0 >= 1 and P32_31 >= 1)
      { P1_0 = P1_0 - 1 , P2_1 = P2_1 + 1 , P32_31 = P32_31 - 1;  }
 transition [priority=9999, intermediate { P2_1 = P2_1 - 1; }]  T2_1 [0,10]
      when (P2_1 >= 1)
      { P2_1 = P2_1 - 1 , P3_2 = P3_2 + 1;  }
 transition [priority=9999, intermediate { P3_2 = P3_2 - 1 , P35_34 = P35_34 - 1; }]  T3_2 [0,0]
      when (P3_2 >= 1 and P35_34 >= 1)
      { P3_2 = P3_2 - 1 , P4_3 = P4_3 + 1 , P35_34 = P35_34 - 1;  }
 transition [priority=9999, intermediate { P4_3 = P4_3 - 1; }]  T4_3 [10,20]
      when (P4_3 >= 1)
      { P4_3 = P4_3 - 1 , P5_4 = P5_4 + 1;  }
 transition [priority=9999, intermediate { P5_4 = P5_4 - 1 , P34_33 = P34_33 - 1; }]  T5_4 [0,0]
      when (P5_4 >= 1 and P34_33 >= 1)
      { P5_4 = P5_4 - 1 , P6_5 = P6_5 + 1 , P34_33 = P34_33 - 1;  }
 transition [priority=9999, intermediate { P6_5 = P6_5 - 1; }]  T6_5 [20,30]
      when (P6_5 >= 1)
      { P6_5 = P6_5 - 1 , P7_6 = P7_6 + 1 , P34_33 = P34_33 + 1;  }
 transition [priority=9999, intermediate { P7_6 = P7_6 - 1; }]  T7_6 [30,40]
      when (P7_6 >= 1)
      { P7_6 = P7_6 - 1 , P8_7 = P8_7 + 1 , P35_34 = P35_34 + 1;  }
 transition [priority=9999, intermediate { P8_7 = P8_7 - 1; }]  T8_7 [40,80]
      when (P8_7 >= 1)
      { P8_7 = P8_7 - 1 , P9_8 = P9_8 + 1 , P32_31 = P32_31 + 1;  }
 transition [priority=9899, intermediate { P10_9 = P10_9 - 1 , P33_32 = P33_32 - 1; }]  T9_8 [0,0]
      when (P10_9 >= 1 and P33_32 >= 1)
      { P10_9 = P10_9 - 1 , P11_10 = P11_10 + 1 , P33_32 = P33_32 - 1;  }
 transition [priority=9899, intermediate { P11_10 = P11_10 - 1; }]  T10_9 [0,5]
      when (P11_10 >= 1)
      { P11_10 = P11_10 - 1 , P12_11 = P12_11 + 1;  }
 transition [priority=9899, intermediate { P12_11 = P12_11 - 1 , P36_35 = P36_35 - 1; }]  T11_10 [0,0]
      when (P12_11 >= 1 and P36_35 >= 1)
      { P12_11 = P12_11 - 1 , P13_12 = P13_12 + 1 , P36_35 = P36_35 - 1;  }
 transition [priority=9899, intermediate { P13_12 = P13_12 - 1; }]  T12_11 [5,10]
      when (P13_12 >= 1)
      { P13_12 = P13_12 - 1 , P14_13 = P14_13 + 1;  }
 transition [priority=9899, intermediate { P14_13 = P14_13 - 1 , P35_34 = P35_34 - 1; }]  T13_12 [0,0]
      when (P14_13 >= 1 and P35_34 >= 1)
      { P14_13 = P14_13 - 1 , P15_14 = P15_14 + 1 , P35_34 = P35_34 - 1;  }
 transition [priority=9899, intermediate { P15_14 = P15_14 - 1; }]  T14_13 [10,15]
      when (P15_14 >= 1)
      { P15_14 = P15_14 - 1 , P16_15 = P16_15 + 1 , P35_34 = P35_34 + 1;  }
 transition [priority=9899, intermediate { P16_15 = P16_15 - 1; }]  T15_14 [15,20]
      when (P16_15 >= 1)
      { P16_15 = P16_15 - 1 , P17_16 = P17_16 + 1 , P36_35 = P36_35 + 1;  }
 transition [priority=9899, intermediate { P17_16 = P17_16 - 1; }]  T16_15 [20,30]
      when (P17_16 >= 1)
      { P17_16 = P17_16 - 1 , P18_17 = P18_17 + 1 , P33_32 = P33_32 + 1;  }
 transition [priority=9799, intermediate { P19_18 = P19_18 - 1 , P32_31 = P32_31 - 1; }]  T17_16 [0,0]
      when (P19_18 >= 1 and P32_31 >= 1)
      { P19_18 = P19_18 - 1 , P20_19 = P20_19 + 1 , P32_31 = P32_31 - 1;  }
 transition [priority=9799, intermediate { P20_19 = P20_19 - 1; }]  T18_17 [0,10]
      when (P20_19 >= 1)
      { P20_19 = P20_19 - 1 , P21_20 = P21_20 + 1;  }
 transition [priority=9799, intermediate { P21_20 = P21_20 - 1 , P34_33 = P34_33 - 1; }]  T19_18 [0,0]
      when (P21_20 >= 1 and P34_33 >= 1)
      { P21_20 = P21_20 - 1 , P22_21 = P22_21 + 1 , P34_33 = P34_33 - 1;  }
 transition [priority=9799, intermediate { P22_21 = P22_21 - 1; }]  T20_19 [10,20]
      when (P22_21 >= 1)
      { P22_21 = P22_21 - 1 , P23_22 = P23_22 + 1;  }
 transition [priority=9799, intermediate { P23_22 = P23_22 - 1 , P36_35 = P36_35 - 1; }]  T21_20 [0,0]
      when (P23_22 >= 1 and P36_35 >= 1)
      { P23_22 = P23_22 - 1 , P24_23 = P24_23 + 1 , P36_35 = P36_35 - 1;  }
 transition [priority=9799, intermediate { P24_23 = P24_23 - 1; }]  T22_21 [20,30]
      when (P24_23 >= 1)
      { P24_23 = P24_23 - 1 , P25_24 = P25_24 + 1 , P36_35 = P36_35 + 1;  }
 transition [priority=9799, intermediate { P25_24 = P25_24 - 1; }]  T23_22 [30,40]
      when (P25_24 >= 1)
      { P25_24 = P25_24 - 1 , P26_25 = P26_25 + 1 , P34_33 = P34_33 + 1;  }
 transition [priority=9799, intermediate { P26_25 = P26_25 - 1; }]  T24_23 [40,100]
      when (P26_25 >= 1)
      { P26_25 = P26_25 - 1 , P27_26 = P27_26 + 1 , P32_31 = P32_31 + 1;  }
 transition [priority=0, intermediate { P27_26 = P27_26 - 1; }]  T25_24 [0,0]
      when (P27_26 >= 1)
      { P10_9 = P10_9 + 1 , P27_26 = P27_26 - 1;  }
 transition [priority=0, intermediate { P18_17 = P18_17 - 1; }]  T26_25 [0,0]
      when (P18_17 >= 1)
      { P1_0 = P1_0 + 1 , P18_17 = P18_17 - 1;  }
 transition [priority=0, intermediate { P28_27 = P28_27 - 1; }]  T27_26 [100,100]
      when (P28_27 >= 1)
      { P19_18 = P19_18 + 1 , P28_27 = P28_27 - 1 + 1;  }
 transition [priority=0, intermediate { P29_28 = P29_28 - 1; }]  T28_27 [200,200]
      when (P29_28 >= 1)
      { P1_0 = P1_0 + 1 , P29_28 = P29_28 - 1 + 1;  }
 transition [priority=0, intermediate { P9_8 = P9_8 - 1; }]  T29_28 [0,0]
      when (P9_8 >= 1)
      { P9_8 = P9_8 - 1;  }
 transition [priority=9998, intermediate { P1_0 = P1_0 - 1 , P20_19 = P20_19 - 1; }]  T30_29 [0,0]
      when (P1_0 >= 1 and P20_19 >= 1)
      { P1_0 = P1_0 - 1 , P2_1 = P2_1 + 1 , P20_19 = P20_19 - 1 , P30_29 = P30_29 + 1;  }
 transition [priority=0, intermediate { P3_2 = P3_2 - 1 , P30_29 = P30_29 - 1; }]  T31_30 [0,0]
      when (P3_2 >= 1 and P30_29 >= 1)
      { P3_2 = P3_2 - 1 , P20_19 = P20_19 + 1 , P30_29 = P30_29 - 1;  }
 transition [priority=9998, intermediate { P1_0 = P1_0 - 1 , P25_24 = P25_24 - 1; }]  T32_31 [0,0]
      when (P1_0 >= 1 and P25_24 >= 1)
      { P1_0 = P1_0 - 1 , P2_1 = P2_1 + 1 , P25_24 = P25_24 - 1 , P31_30 = P31_30 + 1;  }
 transition [priority=0, intermediate { P3_2 = P3_2 - 1 , P31_30 = P31_30 - 1; }]  T33_32 [0,0]
      when (P3_2 >= 1 and P31_30 >= 1)
      { P3_2 = P3_2 - 1 , P25_24 = P25_24 + 1 , P31_30 = P31_30 - 1;  }

graph [passed=eq]
