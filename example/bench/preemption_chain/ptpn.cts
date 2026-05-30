// TPN name=PTPN

typedef int place; 

initially { 
place C2entry=1, C2ready=0, C2exit=0, Bentry=0, Bready=0, Bexit=0, Dentry=0, Dready=0, Dexit=0, C1entry=1, C1ready=0, C1exit=0, Aentry=1, Aready=0, Aexit=0, B_suspended_D_0=0, A_suspended_D_1=0, A_suspended_B_2=0, core0=1, core1=1, core2=1; }

 transition [priority=1099, intermediate { C2entry = C2entry - 1 , core2 = core2 - 1; }]  C2get_core [0,0]
      when (C2entry >= 1 and core2 >= 1)
      { C2entry = C2entry - 1 , C2ready = C2ready + 1 , core2 = core2 - 1;  }
 transition [priority=1099, intermediate { C2ready = C2ready - 1; }]  C2exec [5,5]
      when (C2ready >= 1)
      { C2ready = C2ready - 1 , C2exit = C2exit + 1 , core2 = core2 + 1;  }
 transition [priority=2099, intermediate { Bentry = Bentry - 1 , core0 = core0 - 1; }]  Bget_core [0,0]
      when (Bentry >= 1 and core0 >= 1)
      { Bentry = Bentry - 1 , Bready = Bready + 1 , core0 = core0 - 1;  }
 transition [priority=2099, intermediate { Bready = Bready - 1; }]  Bexec [1,2]
      when (Bready >= 1)
      { Bready = Bready - 1 , Bexit = Bexit + 1 , core0 = core0 + 1;  }
 transition [priority=3099, intermediate { Dentry = Dentry - 1 , core0 = core0 - 1; }]  Dget_core [0,0]
      when (Dentry >= 1 and core0 >= 1)
      { Dentry = Dentry - 1 , Dready = Dready + 1 , core0 = core0 - 1;  }
 transition [priority=3099, intermediate { Dready = Dready - 1; }]  Dexec [2,3]
      when (Dready >= 1)
      { Dready = Dready - 1 , Dexit = Dexit + 1 , core0 = core0 + 1;  }
 transition [priority=1099, intermediate { C1entry = C1entry - 1 , core1 = core1 - 1; }]  C1get_core [0,0]
      when (C1entry >= 1 and core1 >= 1)
      { C1entry = C1entry - 1 , C1ready = C1ready + 1 , core1 = core1 - 1;  }
 transition [priority=1099, intermediate { C1ready = C1ready - 1; }]  C1exec [2,2]
      when (C1ready >= 1)
      { C1ready = C1ready - 1 , C1exit = C1exit + 1 , core1 = core1 + 1;  }
 transition [priority=1099, intermediate { Aentry = Aentry - 1 , core0 = core0 - 1; }]  Aget_core [0,0]
      when (Aentry >= 1 and core0 >= 1)
      { Aentry = Aentry - 1 , Aready = Aready + 1 , core0 = core0 - 1;  }
 transition [priority=1099, intermediate { Aready = Aready - 1; }]  Aexec [8,10]
      when (Aready >= 1)
      { Aready = Aready - 1 , Aexit = Aexit + 1 , core0 = core0 + 1;  }
 transition [priority=0, intermediate { C1exit = C1exit - 1; }]  C1_to_B [0,0]
      when (C1exit >= 1)
      { Bentry = Bentry + 1 , C1exit = C1exit - 1;  }
 transition [priority=0, intermediate { C2exit = C2exit - 1; }]  C2_to_D [0,0]
      when (C2exit >= 1)
      { C2exit = C2exit - 1 , Dentry = Dentry + 1;  }
 transition [priority=0, intermediate { Aexit = Aexit - 1; }]  A_consume [0,0]
      when (Aexit >= 1)
      { Aexit = Aexit - 1;  }
 transition [priority=0, intermediate { Bexit = Bexit - 1; }]  B_consume [0,0]
      when (Bexit >= 1)
      { Bexit = Bexit - 1;  }
 transition [priority=0, intermediate { Dexit = Dexit - 1; }]  D_consume [0,0]
      when (Dexit >= 1)
      { Dexit = Dexit - 1;  }
 transition [priority=3098, intermediate { Bready = Bready - 1 , Dentry = Dentry - 1; }]  D_resume_preempt_B_0 [0,0]
      when (Bready >= 1 and Dentry >= 1)
      { Bready = Bready - 1 , Dentry = Dentry - 1 , Dready = Dready + 1 , B_suspended_D_0 = B_suspended_D_0 + 1;  }
 transition [priority=0, intermediate { Dexit = Dexit - 1 , B_suspended_D_0 = B_suspended_D_0 - 1; }]  B_resume_D_0 [0,0]
      when (Dexit >= 1 and B_suspended_D_0 >= 1)
      { Bready = Bready + 1 , Dexit = Dexit - 1 + 1 , B_suspended_D_0 = B_suspended_D_0 - 1;  }
 transition [priority=3097, intermediate { Dentry = Dentry - 1 , Aready = Aready - 1; }]  D_resume_preempt_A_1 [0,0]
      when (Dentry >= 1 and Aready >= 1)
      { Dentry = Dentry - 1 , Dready = Dready + 1 , Aready = Aready - 1 , A_suspended_D_1 = A_suspended_D_1 + 1;  }
 transition [priority=0, intermediate { Dexit = Dexit - 1 , A_suspended_D_1 = A_suspended_D_1 - 1; }]  A_resume_D_1 [0,0]
      when (Dexit >= 1 and A_suspended_D_1 >= 1)
      { Dexit = Dexit - 1 + 1 , Aready = Aready + 1 , A_suspended_D_1 = A_suspended_D_1 - 1;  }
 transition [priority=2098, intermediate { Bentry = Bentry - 1 , Aready = Aready - 1; }]  B_resume_preempt_A_2 [0,0]
      when (Bentry >= 1 and Aready >= 1)
      { Bentry = Bentry - 1 , Bready = Bready + 1 , Aready = Aready - 1 , A_suspended_B_2 = A_suspended_B_2 + 1;  }
 transition [priority=0, intermediate { Bexit = Bexit - 1 , A_suspended_B_2 = A_suspended_B_2 - 1; }]  A_resume_B_2 [0,0]
      when (Bexit >= 1 and A_suspended_B_2 >= 1)
      { Bexit = Bexit - 1 + 1 , Aready = Aready + 1 , A_suspended_B_2 = A_suspended_B_2 - 1;  }

graph [passed=eq]
