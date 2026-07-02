// TPN name=PTPN

typedef int place; 

initially { 
place Fentry=0, Fready=0, Fexit=0, Eentry=0, Eready=0, Eexit=0, Bentry=0, Bready=0, Bexit=0, Dentry=0, Dready=0, Dexit=0, Centry=0, Cready=0, Cexit=0, Aentry=0, Aready=0, Aexit=0, A_period=1, D_period=1; }

 transition [priority=50, intermediate { Fentry = Fentry - 1; }]  Fget_core [0,0]
      when (Fentry >= 1)
      { Fentry = Fentry - 1 , Fready = Fready + 1;  }
 transition [priority=50, intermediate { Fready = Fready - 1; }]  Fexec [3,3]
      when (Fready >= 1)
      { Fready = Fready - 1 , Fexit = Fexit + 1;  }
 transition [priority=50, intermediate { Eentry = Eentry - 1; }]  Eget_core [0,0]
      when (Eentry >= 1)
      { Eentry = Eentry - 1 , Eready = Eready + 1;  }
 transition [priority=50, intermediate { Eready = Eready - 1; }]  Eexec [15,18]
      when (Eready >= 1)
      { Eready = Eready - 1 , Eexit = Eexit + 1;  }
 transition [priority=50, intermediate { Bentry = Bentry - 1; }]  Bget_core [0,0]
      when (Bentry >= 1)
      { Bentry = Bentry - 1 , Bready = Bready + 1;  }
 transition [priority=50, intermediate { Bready = Bready - 1; }]  Bexec [3,5]
      when (Bready >= 1)
      { Bready = Bready - 1 , Bexit = Bexit + 1;  }
 transition [priority=50, intermediate { Dentry = Dentry - 1; }]  Dget_core [0,0]
      when (Dentry >= 1)
      { Dentry = Dentry - 1 , Dready = Dready + 1;  }
 transition [priority=50, intermediate { Dready = Dready - 1; }]  Dexec [6,8]
      when (Dready >= 1)
      { Dready = Dready - 1 , Dexit = Dexit + 1;  }
 transition [priority=50, intermediate { Centry = Centry - 1; }]  Cget_core [0,0]
      when (Centry >= 1)
      { Centry = Centry - 1 , Cready = Cready + 1;  }
 transition [priority=50, intermediate { Cready = Cready - 1; }]  Cexec [8,10]
      when (Cready >= 1)
      { Cready = Cready - 1 , Cexit = Cexit + 1;  }
 transition [priority=50, intermediate { Aentry = Aentry - 1; }]  Aget_core [0,0]
      when (Aentry >= 1)
      { Aentry = Aentry - 1 , Aready = Aready + 1;  }
 transition [priority=50, intermediate { Aready = Aready - 1; }]  Aexec [3,8]
      when (Aready >= 1)
      { Aready = Aready - 1 , Aexit = Aexit + 1;  }
 transition [priority=0, intermediate { Aexit = Aexit - 1; }]  A_to_B [0,0]
      when (Aexit >= 1)
      { Bentry = Bentry + 1 , Aexit = Aexit - 1;  }
 transition [priority=0, intermediate { Bexit = Bexit - 1; }]  B_to_C [0,0]
      when (Bexit >= 1)
      { Bexit = Bexit - 1 , Centry = Centry + 1;  }
 transition [priority=0, intermediate { Dexit = Dexit - 1; }]  D_to_E [0,0]
      when (Dexit >= 1)
      { Eentry = Eentry + 1 , Dexit = Dexit - 1;  }
 transition [priority=0, intermediate { Eexit = Eexit - 1; }]  E_to_C [0,0]
      when (Eexit >= 1)
      { Eexit = Eexit - 1 , Centry = Centry + 1;  }
 transition [priority=0, intermediate { Dexit = Dexit - 1; }]  D_to_F [0,0]
      when (Dexit >= 1)
      { Fentry = Fentry + 1 , Dexit = Dexit - 1;  }
 transition [priority=0, intermediate { A_period = A_period - 1; }]  A_fire [100,100]
      when (A_period >= 1)
      { Aentry = Aentry + 1 , A_period = A_period - 1 + 1;  }
 transition [priority=0, intermediate { D_period = D_period - 1; }]  D_fire [50,50]
      when (D_period >= 1)
      { Dentry = Dentry + 1 , D_period = D_period - 1 + 1;  }
 transition [priority=0, intermediate { Cexit = Cexit - 1; }]  C_consume [0,0]
      when (Cexit >= 1)
      { Cexit = Cexit - 1;  }
 transition [priority=0, intermediate { Fexit = Fexit - 1; }]  F_consume [0,0]
      when (Fexit >= 1)
      { Fexit = Fexit - 1;  }

graph [passed=eq]
