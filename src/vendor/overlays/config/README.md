`/oem/r800dev/android/overlay/config/config.xml` on the watch is the stock
product/overlay config with our overlays added to the immutable list, so a
data reset cannot leave them disabled:

    <overlay package="com.r800.overlay.pinner" mutable="false" enabled="true" />
    <overlay package="com.r800.overlay.orbita" mutable="false" enabled="true" />
