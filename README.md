# elf2rprc

Convert ELF file into TI RPRC image format.  
Same function as TI out2rprc, but re-implemented and open source  
Support only ELF file, not COFF file.  
It basically packs sections with PROGBITS and SHF_ALLOC flag, then skip everything else.

## Usage

```
elf2rprc <input elf file> <output rprc file>
```

## Build Dependencies

```
sudo apt-get install build-essential pkg-config libelf
```

## Build

```
make
```

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details
