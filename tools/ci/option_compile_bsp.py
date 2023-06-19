import subprocess
import logging
import os

CONFIG_BSP_USING_X = ["CONFIG_BSP_USING_UART", "CONFIG_BSP_USING_I2C", "CONFIG_BSP_USING_SPI", "CONFIG_BSP_USING_PWM"]

def init_logger():
    log_format = "[%(filename)s %(lineno)d %(levelname)s] %(message)s "
    date_format = '%Y-%m-%d  %H:%M:%S %a '
    logging.basicConfig(level=logging.INFO,
                        format=log_format,
                        datefmt=date_format,
                        )

def diff():
    result = subprocess.run(['git', 'diff', '--name-only', 'HEAD', 'origin/master', '--diff-filter=ACMR', '--no-renames', '--full-index'], stdout = subprocess.PIPE)
    file_list = result.stdout.decode().strip().split('\n')
    logging.info(file_list)
    bsp_paths = []
    for file in file_list:
        if "bsp/" in file:
            logging.info("Modifed file: {}".format(file))
            bsp_paths.append(file)
    
    return bsp_paths

def check_config_in_line(line):
    for config in CONFIG_BSP_USING_X:
        if config in line:
            logging.info("Found in {}".format(line))
            return config    
    
    return ""

def check_config_in_file(file_path):
    configs = set()
    found = False
    try:
        with open(file_path, 'r') as file:
            for line in file:
                line.strip()
                if found:
                    res = check_config_in_line(line)
                    if res:
                        configs.add(res)
                elif "On-chip Peripheral Drivers" in line:
                    logging.info("Found On-chip Peripheral Drivers")
                    found = True
    except FileNotFoundError:
        logging.error("The .config file does not exist for this BSP, please recheck the file directory!")

    return configs

def modify_config(file_path, configs): 
    with open(file_path, 'a') as file:
        for item in configs:
            file.write(item + "=y" '\n')

def recompile_bsp(recompile_bsp_dirs):
    logging.info(recompile_bsp_dirs)
    dirs = []
    for dir in recompile_bsp_dirs:
        dir = os.path.dirname(dir)    
        while "bsp/" in dir:
            files = os.listdir(dir)
            if ".config" in files and "rt-thread.elf" not in files and not dir.endswith("bsp"):
                logging.info("Found bsp path: {}".format(dir))
                dirs.append(dir)
                break
            new_dir = os.path.dirname(dir)    
            dir = new_dir
    
    for dir in dirs:
        logging.info("recomplie bsp: {}".format(dir))
        # result = subprocess.run(['scons', '-C', dir + "\\"], stdout = subprocess.PIPE)
        # rtt_root = os.environ["RTT_ROOT"]
        os.system("pwd")
        os.system("ls")
        # os.system("cd bsp")
        os.system("scons -C " + dir)
        # logging.info(result.stdout.decode())
    
    return dirs

if __name__ == '__main__':
    init_logger()
    recompile_bsp_dirs = diff()
    # recompile_bsp_dirs = ["D:/Workspace/OpenSource/rt-thread/bsp/stm32/stm32l496-st-nucleo/rt-config.h"]
    for dir in recompile_bsp_dirs:
        configs = check_config_in_file(dir)
        logging.info("Add configurations and recompile!")
        modify_config(dir, configs)
        recompile_bsp(recompile_bsp_dirs)