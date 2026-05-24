<?xml version="1.0" encoding="UTF-8"?>
<romeo-cts>
  <net>
    <place id="0" name="Fentry" initial="0" capacity="1"/>
    <place id="1" name="Fready" initial="0" capacity="1"/>
    <place id="2" name="Fexit" initial="0" capacity="1"/>
    <place id="3" name="Eentry" initial="0" capacity="1"/>
    <place id="4" name="Eready" initial="0" capacity="1"/>
    <place id="5" name="Eexit" initial="0" capacity="1"/>
    <place id="6" name="Aentry" initial="1" capacity="1"/>
    <place id="7" name="Aready" initial="0" capacity="1"/>
    <place id="8" name="Aexit" initial="0" capacity="1"/>
    <place id="9" name="Dentry" initial="1" capacity="1"/>
    <place id="10" name="Dready" initial="0" capacity="1"/>
    <place id="11" name="Dexit" initial="0" capacity="1"/>
    <place id="12" name="Centry" initial="0" capacity="1"/>
    <place id="13" name="Cready" initial="0" capacity="1"/>
    <place id="14" name="Cexit" initial="0" capacity="1"/>
    <place id="15" name="Bentry" initial="0" capacity="1"/>
    <place id="16" name="Bready" initial="0" capacity="1"/>
    <place id="17" name="Bexit" initial="0" capacity="1"/>
    <place id="18" name="A_period" initial="1" capacity="1"/>
    <place id="19" name="D_period" initial="1" capacity="1"/>
    <place id="20" name="E_suspended_C_0" initial="0" capacity="1"/>
    <place id="21" name="D_suspended_C_1" initial="0" capacity="1"/>
    <place id="22" name="D_suspended_E_2" initial="0" capacity="1"/>
    <place id="23" name="A_suspended_B_3" initial="0" capacity="1"/>
    <place id="24" name="F_suspended_B_4" initial="0" capacity="1"/>
    <place id="25" name="F_suspended_A_5" initial="0" capacity="1"/>
    <place id="26" name="core0" initial="1" capacity="1"/>
    <place id="27" name="core1" initial="1" capacity="1"/>
    <transition id="0" name="Fget_core" priority="9699" earliest="0" latest="0" suspendable="false"/>
    <transition id="1" name="Fexec" priority="9699" earliest="3" latest="3" suspendable="true"/>
    <transition id="2" name="Eget_core" priority="9899" earliest="0" latest="0" suspendable="false"/>
    <transition id="3" name="Eexec" priority="9899" earliest="15" latest="18" suspendable="true"/>
    <transition id="4" name="Aget_core" priority="9799" earliest="0" latest="0" suspendable="false"/>
    <transition id="5" name="Aexec" priority="9799" earliest="3" latest="8" suspendable="true"/>
    <transition id="6" name="Dget_core" priority="9799" earliest="0" latest="0" suspendable="false"/>
    <transition id="7" name="Dexec" priority="9799" earliest="6" latest="8" suspendable="true"/>
    <transition id="8" name="Cget_core" priority="9999" earliest="0" latest="0" suspendable="false"/>
    <transition id="9" name="Cexec" priority="9999" earliest="8" latest="10" suspendable="false"/>
    <transition id="10" name="Bget_core" priority="9899" earliest="0" latest="0" suspendable="false"/>
    <transition id="11" name="Bexec" priority="9899" earliest="3" latest="5" suspendable="false"/>
    <transition id="12" name="A_to_B" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="13" name="B_to_C" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="14" name="D_to_E" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="15" name="E_to_C" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="16" name="A_fire" priority="0" earliest="100" latest="100" suspendable="false"/>
    <transition id="17" name="D_fire" priority="0" earliest="50" latest="50" suspendable="false"/>
    <transition id="18" name="C_consume" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="19" name="F_consume" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="20" name="C_resume_preempt_E_0" priority="9998" earliest="0" latest="0" suspendable="false"/>
    <transition id="21" name="E_resume_C_0" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="22" name="C_resume_preempt_D_1" priority="9997" earliest="0" latest="0" suspendable="false"/>
    <transition id="23" name="D_resume_C_1" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="24" name="E_resume_preempt_D_2" priority="9898" earliest="0" latest="0" suspendable="false"/>
    <transition id="25" name="D_resume_E_2" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="26" name="B_resume_preempt_A_3" priority="9898" earliest="0" latest="0" suspendable="false"/>
    <transition id="27" name="A_resume_B_3" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="28" name="B_resume_preempt_F_4" priority="9897" earliest="0" latest="0" suspendable="false"/>
    <transition id="29" name="F_resume_B_4" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="30" name="A_resume_preempt_F_5" priority="9798" earliest="0" latest="0" suspendable="false"/>
    <transition id="31" name="F_resume_A_5" priority="0" earliest="0" latest="0" suspendable="false"/>
    <arc source="0" target="0" weight="1"/>
    <arc source="1" target="1" weight="1"/>
    <arc source="1" target="28" weight="1"/>
    <arc source="1" target="30" weight="1"/>
    <arc source="2" target="19" weight="1"/>
    <arc source="3" target="2" weight="1"/>
    <arc source="3" target="24" weight="1"/>
    <arc source="4" target="3" weight="1"/>
    <arc source="4" target="20" weight="1"/>
    <arc source="5" target="15" weight="1"/>
    <arc source="5" target="25" weight="1"/>
    <arc source="6" target="4" weight="1"/>
    <arc source="6" target="30" weight="1"/>
    <arc source="7" target="5" weight="1"/>
    <arc source="7" target="26" weight="1"/>
    <arc source="8" target="12" weight="1"/>
    <arc source="8" target="31" weight="1"/>
    <arc source="9" target="6" weight="1"/>
    <arc source="10" target="7" weight="1"/>
    <arc source="10" target="22" weight="1"/>
    <arc source="10" target="24" weight="1"/>
    <arc source="11" target="14" weight="1"/>
    <arc source="12" target="8" weight="1"/>
    <arc source="12" target="20" weight="1"/>
    <arc source="12" target="22" weight="1"/>
    <arc source="13" target="9" weight="1"/>
    <arc source="14" target="18" weight="1"/>
    <arc source="14" target="21" weight="1"/>
    <arc source="14" target="23" weight="1"/>
    <arc source="15" target="10" weight="1"/>
    <arc source="15" target="26" weight="1"/>
    <arc source="15" target="28" weight="1"/>
    <arc source="16" target="11" weight="1"/>
    <arc source="17" target="13" weight="1"/>
    <arc source="17" target="27" weight="1"/>
    <arc source="17" target="29" weight="1"/>
    <arc source="18" target="16" weight="1"/>
    <arc source="19" target="17" weight="1"/>
    <arc source="20" target="21" weight="1"/>
    <arc source="21" target="23" weight="1"/>
    <arc source="22" target="25" weight="1"/>
    <arc source="23" target="27" weight="1"/>
    <arc source="24" target="29" weight="1"/>
    <arc source="25" target="31" weight="1"/>
    <arc source="26" target="0" weight="1"/>
    <arc source="26" target="4" weight="1"/>
    <arc source="26" target="10" weight="1"/>
    <arc source="27" target="2" weight="1"/>
    <arc source="27" target="6" weight="1"/>
    <arc source="27" target="8" weight="1"/>
    <arc source="0" target="1" weight="1"/>
    <arc source="1" target="2" weight="1"/>
    <arc source="1" target="26" weight="1"/>
    <arc source="2" target="4" weight="1"/>
    <arc source="3" target="5" weight="1"/>
    <arc source="3" target="27" weight="1"/>
    <arc source="4" target="7" weight="1"/>
    <arc source="5" target="8" weight="1"/>
    <arc source="5" target="26" weight="1"/>
    <arc source="6" target="10" weight="1"/>
    <arc source="7" target="11" weight="1"/>
    <arc source="7" target="27" weight="1"/>
    <arc source="8" target="13" weight="1"/>
    <arc source="9" target="14" weight="1"/>
    <arc source="9" target="27" weight="1"/>
    <arc source="10" target="16" weight="1"/>
    <arc source="11" target="17" weight="1"/>
    <arc source="11" target="26" weight="1"/>
    <arc source="12" target="15" weight="1"/>
    <arc source="13" target="12" weight="1"/>
    <arc source="14" target="3" weight="1"/>
    <arc source="15" target="12" weight="1"/>
    <arc source="16" target="6" weight="1"/>
    <arc source="16" target="18" weight="1"/>
    <arc source="17" target="9" weight="1"/>
    <arc source="17" target="19" weight="1"/>
    <arc source="20" target="13" weight="1"/>
    <arc source="20" target="20" weight="1"/>
    <arc source="21" target="4" weight="1"/>
    <arc source="22" target="13" weight="1"/>
    <arc source="22" target="21" weight="1"/>
    <arc source="23" target="10" weight="1"/>
    <arc source="24" target="4" weight="1"/>
    <arc source="24" target="22" weight="1"/>
    <arc source="25" target="10" weight="1"/>
    <arc source="26" target="16" weight="1"/>
    <arc source="26" target="23" weight="1"/>
    <arc source="27" target="7" weight="1"/>
    <arc source="28" target="16" weight="1"/>
    <arc source="28" target="24" weight="1"/>
    <arc source="29" target="1" weight="1"/>
    <arc source="30" target="7" weight="1"/>
    <arc source="30" target="25" weight="1"/>
    <arc source="31" target="1" weight="1"/>
  </net>
</romeo-cts>
