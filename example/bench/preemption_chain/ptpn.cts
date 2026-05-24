<?xml version="1.0" encoding="UTF-8"?>
<romeo-cts>
  <net>
    <place id="0" name="Dentry" initial="0" capacity="1"/>
    <place id="1" name="Dready" initial="0" capacity="1"/>
    <place id="2" name="Dexit" initial="0" capacity="1"/>
    <place id="3" name="C2entry" initial="1" capacity="1"/>
    <place id="4" name="C2ready" initial="0" capacity="1"/>
    <place id="5" name="C2exit" initial="0" capacity="1"/>
    <place id="6" name="Bentry" initial="0" capacity="1"/>
    <place id="7" name="Bready" initial="0" capacity="1"/>
    <place id="8" name="Bexit" initial="0" capacity="1"/>
    <place id="9" name="C1entry" initial="1" capacity="1"/>
    <place id="10" name="C1ready" initial="0" capacity="1"/>
    <place id="11" name="C1exit" initial="0" capacity="1"/>
    <place id="12" name="Aentry" initial="1" capacity="1"/>
    <place id="13" name="Aready" initial="0" capacity="1"/>
    <place id="14" name="Aexit" initial="0" capacity="1"/>
    <place id="15" name="B_suspended_D_0" initial="0" capacity="1"/>
    <place id="16" name="A_suspended_D_1" initial="0" capacity="1"/>
    <place id="17" name="A_suspended_B_2" initial="0" capacity="1"/>
    <place id="18" name="core0" initial="1" capacity="1"/>
    <place id="19" name="core1" initial="1" capacity="1"/>
    <place id="20" name="core2" initial="1" capacity="1"/>
    <transition id="0" name="Dget_core" priority="3099" earliest="0" latest="0" suspendable="false"/>
    <transition id="1" name="Dexec" priority="3099" earliest="2" latest="3" suspendable="false"/>
    <transition id="2" name="C2get_core" priority="1099" earliest="0" latest="0" suspendable="false"/>
    <transition id="3" name="C2exec" priority="1099" earliest="5" latest="5" suspendable="false"/>
    <transition id="4" name="Bget_core" priority="2099" earliest="0" latest="0" suspendable="false"/>
    <transition id="5" name="Bexec" priority="2099" earliest="1" latest="2" suspendable="true"/>
    <transition id="6" name="C1get_core" priority="1099" earliest="0" latest="0" suspendable="false"/>
    <transition id="7" name="C1exec" priority="1099" earliest="2" latest="2" suspendable="false"/>
    <transition id="8" name="Aget_core" priority="1099" earliest="0" latest="0" suspendable="false"/>
    <transition id="9" name="Aexec" priority="1099" earliest="8" latest="10" suspendable="true"/>
    <transition id="10" name="C1_to_B" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="11" name="C2_to_D" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="12" name="A_consume" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="13" name="B_consume" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="14" name="D_consume" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="15" name="D_resume_preempt_B_0" priority="3098" earliest="0" latest="0" suspendable="false"/>
    <transition id="16" name="B_resume_D_0" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="17" name="D_resume_preempt_A_1" priority="3097" earliest="0" latest="0" suspendable="false"/>
    <transition id="18" name="A_resume_D_1" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="19" name="B_resume_preempt_A_2" priority="2098" earliest="0" latest="0" suspendable="false"/>
    <transition id="20" name="A_resume_B_2" priority="0" earliest="0" latest="0" suspendable="false"/>
    <arc source="0" target="0" weight="1"/>
    <arc source="0" target="15" weight="1"/>
    <arc source="0" target="17" weight="1"/>
    <arc source="1" target="1" weight="1"/>
    <arc source="2" target="14" weight="1"/>
    <arc source="2" target="16" weight="1"/>
    <arc source="2" target="18" weight="1"/>
    <arc source="3" target="2" weight="1"/>
    <arc source="4" target="3" weight="1"/>
    <arc source="5" target="11" weight="1"/>
    <arc source="6" target="4" weight="1"/>
    <arc source="6" target="19" weight="1"/>
    <arc source="7" target="5" weight="1"/>
    <arc source="7" target="15" weight="1"/>
    <arc source="8" target="13" weight="1"/>
    <arc source="8" target="20" weight="1"/>
    <arc source="9" target="6" weight="1"/>
    <arc source="10" target="7" weight="1"/>
    <arc source="11" target="10" weight="1"/>
    <arc source="12" target="8" weight="1"/>
    <arc source="13" target="9" weight="1"/>
    <arc source="13" target="17" weight="1"/>
    <arc source="13" target="19" weight="1"/>
    <arc source="14" target="12" weight="1"/>
    <arc source="15" target="16" weight="1"/>
    <arc source="16" target="18" weight="1"/>
    <arc source="17" target="20" weight="1"/>
    <arc source="18" target="0" weight="1"/>
    <arc source="18" target="4" weight="1"/>
    <arc source="18" target="8" weight="1"/>
    <arc source="19" target="6" weight="1"/>
    <arc source="20" target="2" weight="1"/>
    <arc source="0" target="1" weight="1"/>
    <arc source="1" target="2" weight="1"/>
    <arc source="1" target="18" weight="1"/>
    <arc source="2" target="4" weight="1"/>
    <arc source="3" target="5" weight="1"/>
    <arc source="3" target="20" weight="1"/>
    <arc source="4" target="7" weight="1"/>
    <arc source="5" target="8" weight="1"/>
    <arc source="5" target="18" weight="1"/>
    <arc source="6" target="10" weight="1"/>
    <arc source="7" target="11" weight="1"/>
    <arc source="7" target="19" weight="1"/>
    <arc source="8" target="13" weight="1"/>
    <arc source="9" target="14" weight="1"/>
    <arc source="9" target="18" weight="1"/>
    <arc source="10" target="6" weight="1"/>
    <arc source="11" target="0" weight="1"/>
    <arc source="15" target="1" weight="1"/>
    <arc source="15" target="15" weight="1"/>
    <arc source="16" target="7" weight="1"/>
    <arc source="17" target="1" weight="1"/>
    <arc source="17" target="16" weight="1"/>
    <arc source="18" target="13" weight="1"/>
    <arc source="19" target="7" weight="1"/>
    <arc source="19" target="17" weight="1"/>
    <arc source="20" target="13" weight="1"/>
  </net>
</romeo-cts>
