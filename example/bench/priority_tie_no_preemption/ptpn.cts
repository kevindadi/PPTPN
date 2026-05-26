// TPN name=PTPN

typedef int place; 

initially { 
place Bentry=0, Bready=0, Bexit=0, Centry=1, Cready=0, Cexit=0, Aentry=1, Aready=0, Aexit=0, core0=1, core1=1; }

 transition [priority=1099, intermediate { Bentry = Bentry - 1 , core0 = core0 - 1; }]  Bget_core [0,0]
      when (Bentry >= 1 and core0 >= 1)
      { Bentry = Bentry - 1 , Bready = Bready + 1 , core0 = core0 - 1;  }
 transition [priority=1099, intermediate { Bready = Bready - 1; }]  Bexec [1,2]
      when (Bready >= 1)
      { Bready = Bready - 1 , Bexit = Bexit + 1 , core0 = core0 + 1;  }
 transition [priority=1099, intermediate { Centry = Centry - 1 , core1 = core1 - 1; }]  Cget_core [0,0]
      when (Centry >= 1 and core1 >= 1)
      { Centry = Centry - 1 , Cready = Cready + 1 , core1 = core1 - 1;  }
 transition [priority=1099, intermediate { Cready = Cready - 1; }]  Cexec [2,2]
      when (Cready >= 1)
      { Cready = Cready - 1 , Cexit = Cexit + 1 , core1 = core1 + 1;  }
 transition [priority=1099, intermediate { Aentry = Aentry - 1 , core0 = core0 - 1; }]  Aget_core [0,0]
      when (Aentry >= 1 and core0 >= 1)
      { Aentry = Aentry - 1 , Aready = Aready + 1 , core0 = core0 - 1;  }
 transition [priority=1099, intermediate { Aready = Aready - 1; }]  Aexec [3,5]
      when (Aready >= 1)
      { Aready = Aready - 1 , Aexit = Aexit + 1 , core0 = core0 + 1;  }
 transition [priority=0, intermediate { Cexit = Cexit - 1; }]  C_to_B [0,0]
      when (Cexit >= 1)
      { Bentry = Bentry + 1 , Cexit = Cexit - 1;  }
 transition [priority=0, intermediate { Aexit = Aexit - 1; }]  A_consume [0,0]
      when (Aexit >= 1)
      { Aexit = Aexit - 1;  }
 transition [priority=0, intermediate { Bexit = Bexit - 1; }]  B_consume [0,0]
      when (Bexit >= 1)
      { Bexit = Bexit - 1;  }

graph [passed=eq]
