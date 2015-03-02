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
--  Description
--  -----------
--     Given a stream of sparse words this block will produce a stream of
--     indexeses indictating where the bits are set. An input fifo is
--     included in this block seeing the output may require multiple
--     clock cycles given a single input.
--
--     Each word takes a minimum of 1 cycle to process. If N bits
--     are set in a word then N cycles are required. The output
--     of the block is registered.
--
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library capi;
use capi.misc.all;

entity list_bits_set is
    generic (
        DATA_WIDTH           : positive := 32;
        INPUT_FIFO_ADDR_BITS : positive := 6);
    port (
        clk     : in  std_logic;
        en      : in  std_logic := '1';
        clear   : in  std_logic := '0';
        empty   : out std_logic;

        -- Input
        din      : in  std_logic_vector(DATA_WIDTH-1 downto 0);
        din_vld  : in  std_logic;
        full     : out std_logic;

        -- Output
        word    : out unsigned(31 downto 0);
        bit_idx : out unsigned(log2_ceil(DATA_WIDTH)-1 downto 0);
        vld     : out std_logic

        );
end entity list_bits_set;

architecture main of list_bits_set is

    signal fifo_read      : std_logic;
    signal fifo_valid     : std_logic;
    signal fifo_write     : std_logic;
    signal fifo_full      : std_logic;
    signal fifo_data      : std_logic_vector(din'range);
    signal fifo_empty     : std_logic;

    signal current       : std_logic_vector(din'range) := (others=>'0');
    signal current_valid : std_logic := '0';

    signal bit_idx_i : unsigned(log2_ceil(DATA_WIDTH)-1 downto 0);
    signal vld_i     : std_logic;
    signal mult_set  : std_logic;

    signal word_i    : unsigned(31 downto 0);
begin

    -- purpose: Buffer the input seeing the output takes one cycle
    --   per set bit.
    INPUT_FIFO : entity capi.sync_fifo_fwft
        generic map (
            DATA_BITS   => din'length,
            ADDR_BITS   => INPUT_FIFO_ADDR_BITS,
            WRITE_SLACK => 10)
        port map (
            clk         => clk,
            write       => fifo_write,
            write_data  => din,
            full        => fifo_full,
            read        => fifo_read,
            read_data   => fifo_data,
            read_valid  => fifo_valid,
            empty       => fifo_empty);

    fifo_write <= din_vld;
    full <= fifo_full;

    -- purpose: Get the index of the least-significant set bit and whether
    --  one or more bits are set.
    BITS_SET_I : entity work.bits_set
        generic map (
            width    => din'length,
            step     => 8)
        port map (
            d        => current,
            frst     => bit_idx_i,
            one_set  => vld_i,
            mult_set => mult_set);

    --purpose: Retrieve the next word from the FIFO or clear one of
    --  the set bits.
    fifo_read  <= en and (not current_valid or not mult_set);
    NEXT_DATA : process (clk) is
    begin
        if rising_edge(clk) then
            if fifo_read = '1' then
                current                        <= fifo_data;
                current_valid                  <= fifo_valid;
            elsif en = '1' then
                current(to_integer(bit_idx_i)) <= '0';
            end if;
        end if;
    end process NEXT_DATA;

    -- purpose: Count the words
    WORD_COUNT : process (clk) is
    begin
        if rising_edge(clk) then
            if clear = '1' then
                word_i  <= (others => '1');
            elsif fifo_read = '1' and fifo_valid = '1' then
                word_i <= word_i + 1;
            end if;
        end if;
    end process WORD_COUNT;

    -- purpose: register the outputs
    REG_OUTPUT: process (clk) is
    begin
        if rising_edge(clk) and en = '1' then
            bit_idx <= bit_idx_i;
            vld     <= vld_i and current_valid;
            word    <= word_i;
            empty   <= fifo_empty and not current_valid;
        end if;
    end process REG_OUTPUT;

end architecture;
