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
--                 Processor Multiplexor Block
--
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library capi;
use capi.misc.all;

entity processors is
    port (
        clk   : in std_logic;
        clear : in std_logic;

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
        flags   : in  std_logic_vector(0 to 7);

        -- Register Interface
        reg_en       : in  std_logic;
        reg_addr     : in  unsigned(0 to 5);
        reg_dw       : in  std_logic;
        reg_write    : in  std_logic;
        reg_wdata    : in  std_logic_vector(0 to 63);
        reg_read     : in  std_logic;
        reg_rdata    : out std_logic_vector(0 to 63);
        reg_read_ack : out std_logic
        );
end entity processors;

architecture main of processors is
    signal memcpy_en     : std_logic;
    signal memcpy_iready : std_logic;
    signal memcpy_odata  : std_logic_vector(odata'range);
    signal memcpy_ovalid : std_logic;
    signal memcpy_odirty : std_logic;
    signal memcpy_done   : std_logic;

    signal lfsr_en     : std_logic;
    signal lfsr_iready : std_logic;
    signal lfsr_odata  : std_logic_vector(odata'range);
    signal lfsr_ovalid : std_logic;
    signal lfsr_odirty : std_logic;
    signal lfsr_done   : std_logic;

    signal text_en     : std_logic;
    signal text_iready : std_logic;
    signal text_odata  : std_logic_vector(odata'range);
    signal text_ovalid : std_logic;
    signal text_odirty : std_logic;
    signal text_done   : std_logic;

    signal reg_lfsr_seed     : std_logic_vector(0 to 63);
    signal reg_lfsr_seed_set : std_logic;
    signal reg_text_search   : std_logic_vector(0 to 127);
    signal reg_text_clear    : std_logic;
begin

    proc_memcpy_i: entity work.proc_memcpy
        port map (
            clk    => clk,
            en     => memcpy_en,
            idata  => idata,
            ivalid => ivalid,
            idone  => idone,
            iready => memcpy_iready,
            odata  => memcpy_odata,
            ovalid => memcpy_ovalid,
            odirty => memcpy_odirty,
            odone  => memcpy_done,
            oready => oready,
            len    => len);

    proc_lfsr_i: entity work.proc_lfsr
        port map (
            clk    => clk,
            en     => lfsr_en,
            idata  => idata,
            ivalid => ivalid,
            idone  => idone,
            iready => lfsr_iready,
            odata  => lfsr_odata,
            ovalid => lfsr_ovalid,
            odirty => lfsr_odirty,
            odone  => lfsr_done,
            oready => oready,
            len    => len,

            reg_lfsr_seed     => reg_lfsr_seed,
            reg_lfsr_seed_set => reg_lfsr_seed_set
            );

    proc_text_i: entity work.proc_text
        port map (
            clk    => clk,
            en     => text_en,
            idata  => idata,
            ivalid => ivalid,
            idone  => idone,
            iready => text_iready,
            odata  => text_odata,
            ovalid => text_ovalid,
            odirty => text_odirty,
            odone  => text_done,
            oready => oready,
            len    => len,

            reg_text_search => reg_text_search,
            reg_text_clear  => reg_text_clear
            );

    iready <= memcpy_iready or
              lfsr_iready or
              text_iready;
    odata  <= memcpy_odata or
              lfsr_odata or
              text_odata;
    ovalid <= memcpy_ovalid or
              lfsr_ovalid or
              text_ovalid;
    odirty <= memcpy_odirty or
              lfsr_odirty or
              text_odirty;
    odone  <= memcpy_done or
              lfsr_done or
              text_done;

    process (clk) is
    begin
        if rising_edge(clk) then
            memcpy_en <= '0';
            lfsr_en   <= '0';
            text_en   <= '0';

            if clear = '0' then
                if flags(0) = '1' then
                    lfsr_en   <= '1';
                elsif flags(1) = '1' then
                    memcpy_en <= '1';
                else
                    text_en   <= '1';
                end if;
            end if;
        end if;
    end process;

    REG_READ_P: process (clk) is
    begin
        if rising_edge(clk) then
            reg_read_ack <= '0';
            reg_rdata <= (others=>'0');

            if reg_en = '1' then
                reg_read_ack <= reg_read;

                case to_integer(reg_addr(0 to 4)) is
                    when 0      => reg_rdata <= reg_lfsr_seed;
                    when 1      => reg_rdata <= endian_swap(reg_text_search(0 to 63));
                    when 2      => reg_rdata <= endian_swap(reg_text_search(64 to 127));
                    when others => reg_rdata <= (others=>'0');
                end case;
            end if;
        end if;
    end process REG_READ_P;

    REG_WRITE_P: process (clk) is
    begin
        if rising_edge(clk) then
            reg_lfsr_seed_set   <= '0';
            reg_text_clear      <= '0';

            if reg_en = '1' and reg_write = '1' then
                case to_integer(reg_addr(0 to 4)) is
                    --We're a little lazy here as we only support 64 bit writes
                    when 0 =>
                        reg_lfsr_seed     <= reg_wdata;
                        reg_lfsr_seed_set <= '1';
                    when 1 =>
                        reg_text_search(0 to 63) <= endian_swap(reg_wdata);
                        reg_text_clear           <= '1';
                    when 2 =>
                        reg_text_search(64 to 127) <= endian_swap(reg_wdata);
                        reg_text_clear             <= '1';
                    when others => null;
                end case;
            end if;
        end if;
    end process REG_WRITE_P;

end architecture main;
