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
--     This block determines the index of the first bit set of a given
--     word. Additionally, it has two flags indicating whether there
--     is a single bit set or multiple bits set. The latency of this
--     block must be zero.
--
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library capi;
use capi.misc.all;

entity bits_set is
    generic (
        width : positive := 32;
        step  : positive := 8);
    port (
        d        : in std_logic_vector(width-1 downto 0);
        frst     : out unsigned(log2_ceil(width)-1 downto 0);
        one_set  : out std_logic;
        mult_set : out std_logic
        );
end entity bits_set;

architecture main of bits_set is
    type frst_arr is array (natural range <>) of unsigned(log2_ceil(step)-1 downto 0);
    signal step_frst : frst_arr(0 to width / step - 1);
    signal step_one  : std_logic_vector(0 to width / step - 1);
    signal step_mult : std_logic_vector(0 to width / step - 1);

    --Lookup table:
    --   Lowest Two Bits:
    --     00 - No bits Set
    --     01 - One bit Set
    --     11 - Multiple bits Set
    --   Highest bits: Index of the first set bit.
    type lut_t is array (0 to 2**step-1) of unsigned(log2_ceil(step)+2-1 downto 0);
    constant lut : lut_t := lut_t'(
        "00000", "00001", "00101", "00011", "01001", "00011", "00111", "00011",
        "01101", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "10001", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "01111", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "10101", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "01111", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "10011", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "01111", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "11001", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "01111", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "10011", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "01111", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "10111", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "01111", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "10011", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "01111", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "11101", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "01111", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "10011", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "01111", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "10111", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "01111", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "10011", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "01111", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "11011", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "01111", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "10011", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "01111", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "10111", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "01111", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "10011", "00011", "00111", "00011", "01011", "00011", "00111", "00011",
        "01111", "00011", "00111", "00011", "01011", "00011", "00111", "00011");
begin

    STEPS: for i in step_frst'range generate
        step_frst(i) <= lut(to_integer(unsigned(d((i+1)*8-1 downto i*8))))
                        (step_frst(i)'high+2 downto 2);
        step_one(i)  <= lut(to_integer(unsigned(d((i+1)*8-1 downto i*8))))(0);
        step_mult(i) <= lut(to_integer(unsigned(d((i+1)*8-1 downto i*8))))(1);
    end generate STEPS;

    -- purpose: Sum the counts and find the first bit
    process (step_frst, step_one, step_mult)
        variable first_one : std_logic;
    begin
        one_set <= '0';
        mult_set <= '0';
        first_one := '0';
        for i in step_one'range loop
            if step_one(i) = '1' then
                one_set <= '1';
                if first_one = '1' then
                    mult_set <= '1';
                end if;
                first_one := '1';
            end if;
            if step_mult(i) = '1' then
                mult_set <= '1';
            end if;
        end loop;

        frst <= (others=>'0');
        for i in step_frst'reverse_range loop
            if step_one(i) = '1' or step_mult(i) = '1' then
                frst <= to_unsigned(i, log2_ceil(width/step)) & step_frst(i);
            end if;
        end loop;
    end process;
end architecture main;
