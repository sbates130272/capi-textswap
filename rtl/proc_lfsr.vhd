--------------------------------------------------------------------------------
--
-- Copyright 2015 PMC-Sierra, Inc.
--
-- Licensed under the Apache License, Version 2.0 (the "License"); you
-- may not use this file except in compliance with the License. You may
-- obtain a copy of the License at
-- http://www.apache.org/licenses/LICENSE-2.0 Unless required by
-- applicable law or agreed to in writing, software distributed under the
-- License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
-- CONDITIONS OF ANY KIND, either express or implied. See the License for
-- the specific language governing permissions and limitations under the
-- License.
--
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
-- Company:        PMC-Sierra, Inc.
-- Engineer:       Logan Gunthorpe
--
-- Description:
--                 LFSR Processor. Write free flowing LFSR data, discard
--                 any read data.
--
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity proc_lfsr is
    port (
        clk   : in std_logic;
        en    : in std_logic;

        idata   : in  std_logic_vector(0 to 511);
        ivalid  : in  std_logic;
        idone   : in  std_logic;
        iready  : out std_logic;
        odata   : out std_logic_vector(0 to 511);
        ovalid  : out std_logic;
        odirty  : out std_logic;
        oready  : in  std_logic;
        odone   : out std_logic;
        len     : in  unsigned(0 to 31);

        reg_lfsr_seed     : in std_logic_vector(0 to 63);
        reg_lfsr_seed_set : in std_logic
        );
end entity proc_lfsr;


architecture main of proc_lfsr is
    signal lfsr_d : std_logic_vector(0 to 511);

    signal count :  unsigned(0 to 30);

    signal ovalid_i : std_logic;

    component lfsr is
        port (
            clk       : in std_logic;
            reset     : in std_logic;
            i_seed    : in std_logic_vector(127 downto 0);
            i_init    : in std_logic;
            i_advance : in std_logic;
            o_lfsr    : out std_logic_vector(511 downto 0)
            );
    end component lfsr;

    signal seed : std_logic_vector(127 downto 0) := (others=>'1');
begin
    ovalid <= ovalid_i;

    seed(63 downto 0) <= reg_lfsr_seed;

    lfsr_i: component lfsr
        port map (
            clk            => clk,
            reset          => '0',
            i_seed         => seed,
            i_init         => reg_lfsr_seed_set,
            i_advance      => '1',
            o_lfsr         => lfsr_d);

    process (clk) is
        variable vcount : unsigned(count'range);
    begin
        if rising_edge(clk) then
            if en = '0' then
                iready   <= '0';
                odata    <= (others=>'0');
                ovalid_i <= '0';
                odirty   <= '0';
                odone    <= '0';
                count    <= (others=>'0');
            else
                iready <= '1';
                odata  <= lfsr_d;
                odirty <= '0';

                vcount := count;
                if oready = '1' and ovalid_i = '1' then
                    vcount := vcount + 1;
                end if;
                count <= vcount;

                if vcount < (len&'0')  then
                    odone    <= '0';
                    ovalid_i <= '1';
                else
                    odone    <= '1';
                    ovalid_i <= '0';
                end if;

            end if;
        end if;
    end process;

end architecture main;
