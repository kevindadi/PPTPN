<?xml version="1.0" encoding="UTF-8"?>
<romeo-cts>
  <net>
    <place id="0" name="Bentry" initial="0" capacity="1"/>
    <place id="1" name="Bready" initial="0" capacity="1"/>
    <place id="2" name="Bexit" initial="0" capacity="1"/>
    <place id="3" name="Centry" initial="1" capacity="1"/>
    <place id="4" name="Cready" initial="0" capacity="1"/>
    <place id="5" name="Cexit" initial="0" capacity="1"/>
    <place id="6" name="Aentry" initial="1" capacity="1"/>
    <place id="7" name="Aready" initial="0" capacity="1"/>
    <place id="8" name="Aexit" initial="0" capacity="1"/>
    <place id="9" name="core0" initial="1" capacity="1"/>
    <place id="10" name="core1" initial="1" capacity="1"/>
    <place id="11" name="core2" initial="1" capacity="1"/>
    <transition id="0" name="Bget_core" priority="10099" earliest="0" latest="0" suspendable="false"/>
    <transition id="1" name="Bexec" priority="10099" earliest="2" latest="3" suspendable="false"/>
    <transition id="2" name="Cget_core" priority="1099" earliest="0" latest="0" suspendable="false"/>
    <transition id="3" name="Cexec" priority="1099" earliest="1" latest="1" suspendable="false"/>
    <transition id="4" name="Aget_core" priority="1099" earliest="0" latest="0" suspendable="false"/>
    <transition id="5" name="Aexec" priority="1099" earliest="4" latest="6" suspendable="false"/>
    <transition id="6" name="C_to_B" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="7" name="A_consume" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="8" name="B_consume" priority="0" earliest="0" latest="0" suspendable="false"/>
    <arc source="0" target="0" weight="1"/>
    <arc source="1" target="1" weight="1"/>
    <arc source="2" target="8" weight="1"/>
    <arc source="3" target="2" weight="1"/>
    <arc source="4" target="3" weight="1"/>
    <arc source="5" target="6" weight="1"/>
    <arc source="6" target="4" weight="1"/>
    <arc source="7" target="5" weight="1"/>
    <arc source="8" target="7" weight="1"/>
    <arc source="9" target="4" weight="1"/>
    <arc source="10" target="0" weight="1"/>
    <arc source="11" target="2" weight="1"/>
    <arc source="0" target="1" weight="1"/>
    <arc source="1" target="2" weight="1"/>
    <arc source="1" target="10" weight="1"/>
    <arc source="2" target="4" weight="1"/>
    <arc source="3" target="5" weight="1"/>
    <arc source="3" target="11" weight="1"/>
    <arc source="4" target="7" weight="1"/>
    <arc source="5" target="8" weight="1"/>
    <arc source="5" target="9" weight="1"/>
    <arc source="6" target="0" weight="1"/>
  </net>
</romeo-cts>
