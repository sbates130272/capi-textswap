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
--                 This is the top-level afu block
--
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library capi;
use capi.psl.all;
use capi.std_logic_1164_additions.all;

entity afu is

    port (
        -- Command Interface
        ah_cvalid  : out std_logic;
        ah_ctag    : out std_logic_vector(0 to 7);
        ah_ctagpar : out std_logic;
        ah_com     : out std_logic_vector(0 to 12);
        ah_compar  : out std_logic;
        ah_cabt    : out std_logic_vector(0 to 2);
        ah_cea     : out unsigned(0 to 63);
        ah_ceapar  : out std_logic;
        ah_cch     : out std_logic_vector(0 to 15);
        ah_csize   : out unsigned(0 to 11);
        ha_croom   : in  unsigned(0 to 7);

        -- Response Interface
        ha_rvalid      : in std_logic;
        ha_rtag        : in std_logic_vector(0 to 7);
        ha_rtagpar     : in std_logic;
        ha_response    : in std_logic_vector(0 to 7);
        ha_rcredits    : in signed(0 to 8);
        ha_rcachestate : in std_logic_vector(0 to 1);
        ha_rcachepos   : in std_logic_vector(0 to 12);

        -- Buffer Interface
        ha_brvalid  : in  std_logic;
        ha_brtag    : in  std_logic_vector(0 to 7);
        ha_brtagpar : in  std_logic;
        ha_brad     : in  unsigned(0 to 5);
        ah_brlat    : out std_logic_vector(0 to 3);
        ah_brdata   : out std_logic_vector(0 to 511);
        ah_brpar    : out std_logic_vector(0 to 7);
        ha_bwvalid  : in  std_logic;
        ha_bwtag    : in  std_logic_vector(0 to 7);
        ha_bwtagpar : in  std_logic;
        ha_bwad     : in  unsigned(0 to 5);
        ha_bwdata   : in  std_logic_vector(0 to 511);
        ha_bwpar    : in  std_logic_vector(0 to 7);

        -- MMIO Interface
        ha_mmval     : in  std_logic;
        ha_mmcfg     : in  std_logic;
        ha_mmrnw     : in  std_logic;
        ha_mmdw      : in  std_logic;
        ha_mmad      : in  unsigned(0 to 23);
        ha_mmadpar   : in  std_logic;
        ha_mmdata    : in  std_logic_vector(0 to 63);
        ha_mmdatapar : in  std_logic;
        ah_mmack     : out std_logic;
        ah_mmdata    : out std_logic_vector(0 to 63);
        ah_mmdatapar : out std_logic;

        -- Control Interface
        ha_jval : in std_logic;
        ha_jcom : in std_logic_vector(0 to 7);
        ha_jcompar : in std_logic;
        ha_jea : in unsigned(0 to 63);
        ha_jeapar : in std_logic;
        ah_jrunning : out std_logic := '0';
        ah_jdone : out std_logic := '0';
        ah_jcack : out std_logic := '0';
        ah_jerror : out std_logic_vector(0 to 63) := (others=>'0');
        ah_jyield : out std_logic := '0';
        ah_tbreq  : out std_logic := '0';
        ah_paren  : out std_logic := '1';
        ha_pclock : in std_logic
        );

end entity afu;

architecture main of afu is
    signal reset  : std_logic := '0';
    signal start : std_logic  := '0';

    signal reg_addr     : unsigned(0 to 23);
    signal reg_dw       : std_logic;
    signal reg_write    : std_logic;
    signal reg_wdata    : std_logic_vector(0 to 63);
    signal reg_read     : std_logic;
    signal reg_rdata    : std_logic_vector(0 to 63);
    signal reg_read_ack : std_logic;

    signal reg_dead_read_ack : std_logic;

    signal reg_bv_en       : std_logic;
    signal reg_bv_rdata    : std_logic_vector(0 to 63);
    signal reg_bv_read_ack : std_logic;

    signal reg_wq_en       : std_logic;
    signal reg_wq_rdata    : std_logic_vector(0 to 63);
    signal reg_wq_read_ack : std_logic;

    signal reg_sn_en       : std_logic;
    signal reg_sn_rdata    : std_logic_vector(0 to 63);
    signal reg_sn_read_ack : std_logic;

    signal reg_pr_en       : std_logic;
    signal reg_pr_rdata    : std_logic_vector(0 to 63);
    signal reg_pr_read_ack : std_logic;

    signal wed_base_addr : unsigned(0 to 63);

    signal ah_jdone_next    : std_logic;

    signal wqueue_done      : std_logic;
    signal wqueue_done_last : std_logic;

    signal proc_clear   : std_logic;
    signal proc_idata   : std_logic_vector(0 to 511);
    signal proc_ivalid  : std_logic;
    signal proc_iready  : std_logic := '1';
    signal proc_idone   : std_logic;
    signal proc_odata   : std_logic_vector(0 to 511) := (others=>'0');
    signal proc_ovalid  : std_logic := '0';
    signal proc_odirty  : std_logic := '1';
    signal proc_oready  : std_logic;
    signal proc_odone   : std_logic;
    signal proc_len     : unsigned(0 to 31);
    signal proc_flags   : std_logic_vector(0 to 7);

    signal ah_cvalid_i : std_logic;
    signal ah_ctag_i   : std_logic_vector(ah_ctag'range);
    signal ah_com_i    : std_logic_vector(ah_com'range);
    signal ah_cea_i    : unsigned(ah_cea'range);
    signal ah_csize_i  : unsigned(ah_csize'range);

    signal timer : unsigned(0 to 63);
begin
    ah_cvalid <= ah_cvalid_i;
    ah_ctag   <= ah_ctag_i;
    ah_com    <= ah_com_i;
    ah_cea    <= ah_cea_i;
    ah_csize  <= ah_csize_i;

    JOB: process (ha_pclock) is
    begin
        if rising_edge(ha_pclock) then
            start <= '0';

            ah_jdone_next <= '0';
            ah_jdone      <= ah_jdone_next;

            if reset = '1' then
                ah_jrunning <= '0';
                reset       <= '0';
            end if;

            wqueue_done_last <= wqueue_done;
            if wqueue_done = '1' and wqueue_done_last = '0' then
                ah_jrunning   <= '0';
                ah_jdone_next <= '1';
            end if;

            if ha_jval = '1' then
                case ha_jcom is
                    when PSL_CTRL_CMD_START =>
                        ah_jrunning   <= '1';
                        wed_base_addr <= unsigned(ha_jea);
                        start         <= '1';
                    when PSL_CTRL_CMD_RESET =>
                        ah_jrunning   <= '0';
                        reset         <= '1';
                        ah_jdone_next <= '1';
                    when others =>
                        report "Unsupported control command: " & to_hstring(ha_jcom)
                            severity WARNING;

                end case;
            end if;
        end if;
    end process JOB;

    TIMER_P: process (ha_pclock) is
    begin
        if rising_edge(ha_pclock) then
            if reset = '1' then
                timer <= (others=>'0');
            else
                timer <= timer + 1;
            end if;
        end if;
    end process TIMER_P;

    mmio_i: entity capi.mmio
        port map (
            ha_pclock         => ha_pclock,
            reset             => reset,
            ha_mmval          => ha_mmval,
            ha_mmcfg          => ha_mmcfg,
            ha_mmrnw          => ha_mmrnw,
            ha_mmdw           => ha_mmdw,
            ha_mmad           => ha_mmad,
            ha_mmadpar        => ha_mmadpar,
            ha_mmdata         => ha_mmdata,
            ha_mmdatapar      => ha_mmdatapar,
            ah_mmack          => ah_mmack,
            ah_mmdata         => ah_mmdata,
            ah_mmdatapar      => ah_mmdatapar,
            reg_addr          => reg_addr,
            reg_dw            => reg_dw,
            reg_write         => reg_write,
            reg_wdata         => reg_wdata,
            reg_read          => reg_read,
            reg_rdata         => reg_rdata,
            reg_read_ack      => reg_read_ack);

    reg_rdata    <= reg_bv_rdata or
                    reg_wq_rdata or
                    reg_sn_rdata or
                    reg_pr_rdata;
    reg_read_ack <= reg_bv_read_ack or
                    reg_wq_read_ack or
                    reg_sn_read_ack or
                    reg_pr_read_ack or
                    reg_dead_read_ack;

    REG_SEL: process (reg_addr, reg_read) is
    begin
        reg_bv_en   <= '0';
        reg_wq_en   <= '0';
        reg_sn_en   <= '0';
        reg_pr_en   <= '0';

        reg_dead_read_ack <= '0';

        case to_integer(reg_addr(0 to 17)) is
            when 0      => reg_bv_en <= '1';
            when 1      => reg_wq_en <= '1';
            when 2      => reg_sn_en <= '1';
            when 3      => reg_pr_en <= '1';

            when others => reg_dead_read_ack <= reg_read;
        end case;
    end process REG_SEL;

    build_version_i: entity capi.build_version
        port map (
            clk          => ha_pclock,
            reg_en       => reg_bv_en,
            reg_addr     => reg_addr(18 to 23),
            reg_dw       => reg_dw,
            reg_write    => reg_write,
            reg_wdata    => reg_wdata,
            reg_read     => reg_read,
            reg_rdata    => reg_bv_rdata,
            reg_read_ack => reg_bv_read_ack);

    wqueue_i: entity capi.wqueue
        port map (
            ha_pclock         => ha_pclock,
            reset             => reset,
            start             => start,
            timer             => timer,
            wed_base_addr     => wed_base_addr,
            wqueue_done       => wqueue_done,
            ah_cvalid         => ah_cvalid_i,
            ah_ctag           => ah_ctag_i,
            ah_ctagpar        => ah_ctagpar,
            ah_com            => ah_com_i,
            ah_compar         => ah_compar,
            ah_cabt           => ah_cabt,
            ah_cea            => ah_cea_i,
            ah_ceapar         => ah_ceapar,
            ah_cch            => ah_cch,
            ah_csize          => ah_csize_i,
            ha_croom          => ha_croom,
            ha_rvalid         => ha_rvalid,
            ha_rtag           => ha_rtag,
            ha_rtagpar        => ha_rtagpar,
            ha_response       => ha_response,
            ha_rcredits       => ha_rcredits,
            ha_rcachestate    => ha_rcachestate,
            ha_rcachepos      => ha_rcachepos,
            ha_brvalid        => ha_brvalid,
            ha_brtag          => ha_brtag,
            ha_brtagpar       => ha_brtagpar,
            ha_brad           => ha_brad,
            ah_brlat          => ah_brlat,
            ah_brdata         => ah_brdata,
            ah_brpar          => ah_brpar,
            ha_bwvalid        => ha_bwvalid,
            ha_bwtag          => ha_bwtag,
            ha_bwtagpar       => ha_bwtagpar,
            ha_bwad           => ha_bwad,
            ha_bwdata         => ha_bwdata,
            ha_bwpar          => ha_bwpar,
            reg_en            => reg_wq_en,
            reg_addr          => reg_addr(18 to 23),
            reg_dw            => reg_dw,
            reg_write         => reg_write,
            reg_wdata         => reg_wdata,
            reg_read          => reg_read,
            reg_rdata         => reg_wq_rdata,
            reg_read_ack      => reg_wq_read_ack,
            proc_clear        => proc_clear,
            proc_idata        => proc_idata,
            proc_ivalid       => proc_ivalid,
            proc_iready       => proc_iready,
            proc_idone        => proc_idone,
            proc_odata        => proc_odata,
            proc_ovalid       => proc_ovalid,
            proc_odirty       => proc_odirty,
            proc_oready       => proc_oready,
            proc_odone        => proc_odone,
            proc_len          => proc_len,
            proc_flags        => proc_flags
            );

    processors_i: entity work.processors
        port map (
            clk    => ha_pclock,
            clear  => proc_clear,
            idata  => proc_idata,
            ivalid => proc_ivalid,
            idone  => proc_idone,
            iready => proc_iready,
            odata  => proc_odata,
            ovalid => proc_ovalid,
            odirty => proc_odirty,
            oready => proc_oready,
            odone  => proc_odone,
            len    => proc_len,
            flags  => proc_flags,

            reg_en       => reg_pr_en,
            reg_addr     => reg_addr(18 to 23),
            reg_dw       => reg_dw,
            reg_write    => reg_write,
            reg_wdata    => reg_wdata,
            reg_read     => reg_read,
            reg_rdata    => reg_pr_rdata,
            reg_read_ack => reg_pr_read_ack);

    snooper_i: entity capi.snooper
        port map (
            ha_pclock      => ha_pclock,
            reset          => reset,
            ah_cvalid      => ah_cvalid_i,
            ah_ctag        => ah_ctag_i,
            ah_com         => ah_com_i,
            ah_cea         => ah_cea_i,
            ah_csize       => ah_csize_i,
            ha_rvalid      => ha_rvalid,
            ha_rtag        => ha_rtag,
            ha_response    => ha_response,
            ha_rcredits    => ha_rcredits,
            ha_rcachestate => ha_rcachestate,
            ha_rcachepos   => ha_rcachepos,
            ha_bwvalid     => ha_bwvalid,
            ha_bwdata      => ha_bwdata,
            reg_en         => reg_sn_en,
            reg_addr       => reg_addr(18 to 23),
            reg_dw         => reg_dw,
            reg_write      => reg_write,
            reg_wdata      => reg_wdata,
            reg_read       => reg_read,
            reg_rdata      => reg_sn_rdata,
            reg_read_ack   => reg_sn_read_ack
            );

end architecture main;
