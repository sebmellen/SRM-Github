man_MANS = srm.1
EXTRA_DIST = $(man_MANS)

srm.html:	srm.1
	perl man2html.pl $< > $@

install_man:	srm.html
	[ -d ../../www/ ]
	cp $< ../../www/

clean-local:
	rm -f srm.html
