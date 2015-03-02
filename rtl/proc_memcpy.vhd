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
--                 Copy input data to output.
--
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library capi;

entity proc_memcpy is
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
        len     : in  unsigned(0 to 31)
        );
end entity proc_memcpy;


architecture main of proc_memcpy is
    signal fifo_rst   : std_logic;
    signal fifo_write : std_logic;
    signal fifo_full  : std_logic;
    signal fifo_empty : std_logic;
begin
    fifo_rst   <= not en;
    fifo_write <= ivalid and not fifo_full;
    iready     <= not fifo_full;
    odirty     <= '0';

    FIFO: entity capi.sync_fifo_fwft
        generic map (
            WRITE_SLACK => 2,
            DATA_BITS   => idata'length,
            ADDR_BITS   => 3)
        port map (
            clk        => clk,
            rst        => fifo_rst,
            write      => fifo_write,
            write_data => idata,
            full       => fifo_full,
            read       => oready,
            read_valid => ovalid,
            read_data  => odata,
            empty      => fifo_empty);

    DONE_P: process (clk) is
    begin
        if rising_edge(clk) then
            odone      <= fifo_empty and not ivalid and en and idone;
        end if;
    end process DONE_P;

end architecture main;
