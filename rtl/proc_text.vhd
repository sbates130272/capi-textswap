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
--                 Text Processor. Find the locations of a substring in a
--                 string of data.
--
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library capi;
use capi.misc.all;
use capi.std_logic_1164_additions.all;

entity proc_text is
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

        reg_text_search   : in std_logic_vector(0 to 127);
        reg_text_clear    : in std_logic
        );
end entity proc_text;

architecture main of proc_text is
    signal wrap_buf : std_logic_vector(0 to reg_text_search'high - 8);
    signal buf : std_logic_vector(0 to wrap_buf'length + idata'length - 1);

    signal iready_i : std_logic := '1';

    alias needle : std_logic_vector(reg_text_search'range) is reg_text_search;

    signal mask : std_logic_vector(0 to needle'length / 8 - 1);

    type cmpres_t is array (natural range <>) of
        std_logic_vector(0 to needle'length / 8 - 1);
    signal stage1 : cmpres_t(0 to buf'length / 8 - 1);
    signal stage2 : std_logic_vector(stage1'reverse_range);
    signal stage3 : std_logic_vector(stage2'high+1 downto stage2'low);

    signal stage1_valid : std_logic;
    signal stage2_valid : std_logic;
    signal stage3_valid : std_logic;

    function strcmp (haystack, needle : std_logic_vector)
        return std_logic_vector is
        variable ret : std_logic_vector(0 to needle'length / 8 - 1);
    begin
        for i in ret'range loop
            if haystack'low + (i+1)*8-1 > haystack'high then
                ret(i) := '0';
            elsif haystack(haystack'low + i*8 to haystack'low + (i+1)*8-1) =
                needle(i*8 to (i+1)*8-1) then
                ret(i) := '1';
            else
                ret(i) := '0';
            end if;
        end loop;

        return ret;
    end function;

    signal lbs_empty : std_logic;
    signal lbs_full  : std_logic;

    signal lbs_word  : unsigned(31 downto 0);
    signal lbs_bit   : unsigned(log2_ceil(stage2'length)-1 downto 0);
    signal lbs_valid : std_logic;
    signal lbs_done  : std_logic;
    signal lbs_clear : std_logic;

    signal done_next : std_logic;

    signal idx        : signed(0 to 31);
    signal idx_valid  : std_logic;
    signal idx_done   : std_logic;

    subtype outp_count_t is integer range 0 to odata'length / idx'length - 1;
    signal outp_count : outp_count_t := 0;
    signal high_word : std_logic;

    signal odata_next : std_logic_vector(odata'range);

    impure function odata_pad
        return std_logic_vector is
        variable ret : std_logic_vector(odata'range);
    begin
        for i in 0 to odata'length/32 -1 loop
            ret(i*32 to (i+1)*32-1) := endian_swap((0 => '0', 1 to 31 => '1'));
        end loop;

        return ret;
    end function;

begin
    iready <= iready_i;

    buf    <= wrap_buf & idata;

    WRAP_BUF_P: process (clk) is
    begin
        if rising_edge(clk) then
            if reg_text_clear = '1' then
                wrap_buf <= (others=>'0');
            elsif ivalid = '1' and iready_i = '1' then
                wrap_buf <= buf(buf'high-wrap_buf'length+1 to buf'high);
            end if;
        end if;
    end process WRAP_BUF_P;

    COMPARE_P: process (clk) is
    begin
        if rising_edge(clk) then
            mask <= strcmp(needle, (needle'range=>'0'));

            stage1_valid <= ivalid and iready_i;
            stage2_valid <= stage1_valid and or_reduce(mask);
            stage3_valid <= stage2_valid;

            for i in stage1'range loop
                stage1(i) <= strcmp(buf(i*8 to buf'high), needle);
                stage2(i) <= and_reduce(stage1(i) or mask);
            end loop;

            stage3 <= "0" & (stage2 and not resize(mask, stage2'length));
        end if;
    end process COMPARE_P;

    list_bits_set_i: entity work.list_bits_set
        generic map (
            DATA_WIDTH           => stage3'length,
            INPUT_FIFO_ADDR_BITS => 6)
        port map (
            clk     => clk,
            en      => oready,
            clear   => lbs_clear,
            empty   => lbs_empty,
            din     => stage3,
            din_vld => stage3_valid,
            full    => lbs_full,
            word    => lbs_word,
            bit_idx => lbs_bit,
            vld     => lbs_valid);

    DEDUP_P: process (clk) is
    begin
        if rising_edge(clk) and oready = '1' then
            idx       <= signed(resize(lbs_word * (idata'length / 8), idx'length)
                         + resize(lbs_bit, idx'length)) - wrap_buf'length / 8;

            idx_valid <= lbs_valid;
        end if;
    end process DEDUP_P;

    DONE_P: process (clk) is
    begin
        if rising_edge(clk) and oready = '1' then
            lbs_done <= idone and lbs_empty and not stage1_valid and
                        not stage2_valid and not stage3_valid and not ivalid;
            idx_done <= lbs_done and not idx_valid;
            odone   <= done_next;
        end if;
    end process;

    OUT_P: process (clk) is
    begin
        if rising_edge(clk) then
            ovalid <= '0';

            if oready = '1' then
                if idx_valid = '1' then
                    if outp_count = outp_count_t'high then
                        outp_count <= 0;
                        odata <= odata_next(0 to odata_next'high - idx'length) &
                                 endian_swap(std_logic_vector(idx));
                        odata_next <= odata_pad;
                        ovalid <= '1';
                        high_word  <= not high_word;
                    else
                        odata_next(outp_count*idx'length to (outp_count+1)*idx'length-1) <=
                            endian_swap(std_logic_vector(idx));
                        outp_count <= outp_count + 1;
                    end if;
                elsif idx_done = '1' and done_next = '0' then
                    odata      <= odata_next;
                    odata_next <= odata_pad;
                    if outp_count > 0 or high_word = '1' then
                        ovalid     <= not done_next;
                        high_word  <= not high_word;
                    end if;

                    if outp_count > 0 then
                        done_next  <= high_word;
                    else
                        done_next  <= not high_word;
                    end if;
                end if;
            end if;

            if en = '0' then
                odata      <= (others=>'0');
                odata_next <= odata_pad;
                ovalid     <= '0';
                high_word  <= '0';
                outp_count <= 0;
                done_next  <= '0';
            end if;
        end if;
    end process OUT_P;

    FLAGS: process (clk) is
    begin
        if rising_edge(clk) then
            if en = '0' then
                iready_i  <= '0';
                odirty    <= '0';
                lbs_clear <= '1';
            else
                iready_i  <= not lbs_full;
                odirty    <= '1';
                lbs_clear <= '0';
            end if;
        end if;
    end process FLAGS;

end architecture main;
