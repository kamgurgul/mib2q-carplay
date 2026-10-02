# stock/ - the unit's own classes and the host tools (not in Git)

Everything here except this README is gitignored (`stock/jxe/` included, for now): the jar is your firmware's
proprietary code, and the rest is large and downloadable. `scripts/stock_env.sh`
reads this layout.

```text
stock/
├── jxe/             raw .jxe images off the unit, one per firmware if you keep several
│   └── lsd.jxe      /mnt/app/eso/hmi/lsd/lsd.jxe (MHI2Q_ER_AUG22_P5152)
├── base.jar         lsd.jxe converted with luka-dev/jxe2jar (STOCK_JAR)
├── libs/
│   ├── org.osgi.framework-1.10.0.jar     public, Maven Central
│   ├── org.osgi.util.tracker-1.5.4.jar   public, Maven Central
│   ├── asm-9.7.jar, asm-tree-9.7.jar     public, Maven Central (audit/PDC tests only)
│   └── jcl/<firmware>/jcl.jar            the unit's J9 class library (audit only)
└── jdk/             a host JDK 8 home (host Java tests only; Zulu 8 macOS aarch64)
```

Only `base.jar` and the two OSGi jars are needed to **build**
(`scripts/build_java.sh` compiles in Docker). `jdk/`, ASM and `jcl/` are only for
the host test and audit scripts.

## Getting base.jar

1. Pull `lsd.jxe` off the unit: run `extract_lsd_MoreIncredibleBash/`, or
   `scp root@<unit>:/mnt/app/eso/hmi/lsd/lsd.jxe stock/jxe/`.
2. Convert it with [luka-dev/jxe2jar](https://github.com/luka-dev/jxe2jar) (outside this repo):
   ```sh
   python3 src/jxe2jar.py /path/to/stock/jxe/lsd.jxe /path/to/stock/base.jar
   ```

The current `base.jar` was converted from an `MHI2Q_ER_AUG22_P5152` unit. The
`jcl.jar` is from `MHI2Q_US_AUG22_P5087_MU1316`, the closest one available.

## Overrides

`STOCK_DIR`, `STOCK_JAR`, `STOCK_COMBINED_JAR`, `STOCK_VF_DIR`, `STOCK_JCL_JAR` and
`JDK` override each path; see `scripts/stock_env.sh`. The original author's test
scripts used two jxe2jar outputs: `*-final.jar` (after the uninline pass) and
`*-combined.jar` (core plus app-image bundles, before uninlining). Both default to
`base.jar` here. Point `STOCK_COMBINED_JAR` at a combined jar if a PDC or KOMO test
cannot resolve an app-image class.
