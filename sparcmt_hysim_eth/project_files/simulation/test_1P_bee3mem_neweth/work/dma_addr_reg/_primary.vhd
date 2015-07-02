library verilog;
use verilog.vl_types.all;
library work;
entity dma_addr_reg is
    port(
        gclk            : in     work.libiu.iu_clk_type;
        dma_wtid        : in     vl_logic_vector;
        dma_we          : in     vl_logic;
        dma_wdata       : in     work.libdebug.debug_dma_addr_reg_type;
        iu_wtid         : in     vl_logic_vector;
        iu_we           : in     vl_logic;
        iu_wdata        : in     work.libdebug.debug_dma_addr_reg_type;
        iu_rtid         : in     vl_logic_vector;
        iu_rdata        : out    work.libdebug.debug_dma_addr_reg_type
    );
end dma_addr_reg;