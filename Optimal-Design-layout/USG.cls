%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%%
%% This file is copyright by Wiley. All rights reserved.
%%
%% Author:       Wiley, LaTeX Development Team (India)
%% Created On:
%% Last Modified By:
%% Last Modified On:
%% Description:  Class file to support authoring template in LaTeX typesetting
%%               for Wiley Journals
%%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

\NeedsTeXFormat{LaTeX2e}[2009/09/24]
\ProvidesClass{USG}
              [2024/05/07 v6.0 Standard LaTeX document class for Journal]

\def\@p@t{bp}

\def\orchid#1{%
  \href{#1}{\includegraphics[height=7\@p@t]{images/ORCID_Logo}}%
}
\def\facebook#1{%
  \href{#1}{\includegraphics[height=7\@p@t]{images/Facebook_Logo}}%
}
\def\linkedin#1{%
  \href{#1}{\includegraphics[height=7\@p@t]{images/LinkedIn_Logo}}%
}
\def\twitter#1{%
  \href{#1}{\includegraphics[height=7\@p@t]{images/Twitter_Logo}}%
}

\def\specialissue#1{}
\def\subarticletype#1{}
\def\transtitle#1{}
\def\subtranstitle#1{}
\def\transkeywords#1{}

\def\abbr#1{}
\def\contributed#1{}
\def\dedicated#1{}
\def\openaccessstatement{}

\newif\if@cropmarkson\@cropmarksonfalse
\newif\if@croppage\@croppagefalse
\newif\if@draftruleson\@draftrulesonfalse
\newif\if@exactptsize\@exactptsizefalse
\newif\if@pagenum   \@pagenumfalse%
\newif\if@purexml\global\@purexmlfalse
\newif\if@xml\global\@xmlfalse
\newif\if@hyperrefon  \@hyperrefonfalse%
\newif\ifbibunnumbered\global\bibunnumberedtrue%
\newif\ifproof\global\prooffalse%
\newif\ifEqnSpan\global\EqnSpantrue%
\newif\if@dblspace\global\@dblspacefalse%

\newif\if@APArefstyle\global\@APArefstylefalse%
\newif\if@AMSrefstyle\global\@AMSrefstylefalse%
\newif\if@AMArefstyle\global\@AMArefstylefalse%
\newif\if@Vancouverrefstyle\global\@Vancouverrefstylefalse%
\newif\if@Chemistryrefstyle\global\@Chemistryrefstylefalse%
\newif\if@Harvardstyle\global\@Harvardstylefalse%
\newif\if@Chicagostyle\global\@Chicagostylefalse%
\newif\if@MPSstyle\global\@MPSstylefalse%
\newif\if@APSstyle\global\@APSstylefalse%
\newif\if@MLAstyle\global\@MLAstylefalse%


\def\dummyalphabet{\vphantom{abcdefghijklmnopqrstuvwxyz0123456789}}

%\DeclareOption{STIX2COL}{}

\DeclareOption{APA}{\global\@APArefstyletrue}
\DeclareOption{ASNA}{\global\@AMSrefstyletrue}
\DeclareOption{WCMS}{\global\@Chemistryrefstyletrue}
\DeclareOption{AMA}{\global\@AMArefstyletrue}
\DeclareOption{VANCOUVER}{\global\@Vancouverrefstyletrue}

\DeclareOption{HARVARD}{\global\@Harvardstyletrue}
\DeclareOption{CHICAGO}{\global\@Chicagostyletrue}
\DeclareOption{MPS}{\global\@MPSstyletrue}
\DeclareOption{APS}{\global\@APSstyletrue}
\DeclareOption{MLA}{\global\@MLAstyletrue}

\DeclareOption{doublespace}{\global\@dblspacetrue}

\DeclareOption{draftrules}{\global\@draftrulesontrue}
\DeclareOption{exactsize}{\global\@exactptsizetrue}%
\DeclareOption{noexactsize}{\global\@exactptsizefalse}%
\DeclareOption{croppage}{\global\@croppagetrue\global\@cropmarksontrue}%
\DeclareOption{number}{\xdef\@biboptions{numbers}}
\DeclareOption{numbers}{\xdef\@biboptions{numbers}}
\DeclareOption{EqnSpan}{\global\EqnSpantrue}%%

\DeclareOption{proof}{%
  \global\prooftrue%
  \global\@hyperrefontrue%
}%

\DeclareOption{final}{%
  \global\prooffalse%
  \global\@hyperrefontrue% links and bookmarks are removed through PDF action list post PDF creation
}%

\def\@JrnlID{}%
\gdef\JrnlID#1{\gdef\@JrnlID{#1}}%


\def\@YES{YES}%
\def\@NO{NO}%

%
%%%%%%%%% From Boolean Package %%%%%%%%%
%

\newcommand {\TRUE}[2]{#1}
\newcommand {\FALSE}[2]{#2}

%% Public commands are also given inner hidden names

\let \bln@true \TRUE
\let \bln@false \FALSE

\newcommand {\AND}[2]{#1{#2}{\bln@false}}
\newcommand {\OR}[2]{#1{\bln@true}{#2}}
\newcommand {\NOT}[1]{#1{\bln@false}{\bln@true}}
\newcommand {\IF}[3]{#1{#2}{#3}}

\let \bln@and \AND
\let \bln@or  \OR
\let \bln@not \NOT
\let \bln@if  \IF


%% To convert if.. macros into Booleans:

\newcommand {\texbool}[1]
    {#1\let \bln@do \bln@true \else \let \bln@do \bln@false\fi \bln@do}
%% Does not work
\let \bln@tex \texbool

\newcommand {\ifxbool}[2]
    {\ifx #1#2\let \bln@do \bln@true \else \let \bln@do \bln@false \fi \bln@do}
\let \bln@ifx \ifxbool

\newcommand {\ifybool}[2]
    {\def \bln@testa{#1}\ifxbool{\bln@testa}{#2}}
\let \bln@ify \ifybool

\newcommand {\ifequalbool}[2]
    {\def \bln@testa{#1}\def \bln@testb{#2}\ifxbool{\bln@testa}{\bln@testb}}
\let \bln@ifequal \ifequalbool

\def \bln@empty {}
\newcommand {\ifemptybool}[1]
    {\ifybool{#1}{\bln@empty}}
\let \bln@ifempty \ifemptybool

%% To force evaluation, e.g. if a blnlean expression is to be used several
%% times. This takes a Boolean expression and returns a Boolean value, i.e.
%% \True or \False.

\newcommand* {\letboolval}[2]
        {#2{\let #1\bln@true}{\let #1\bln@false}}
\newcommand* {\newboolval}[2]
        {\@ifdefinable #1{\letboolval{#1}{#2}}}

%% We provide list versions of And and Or

\def \bln@scan #1,#2\bln@to#3#4{%
    \bln@ifempty{#2}{#3{#4}{#1}}
          {\bln@scan #2\bln@to {#3}{#3{#4}{#1}}}%
}
\def \bln@eval #1#2#3{%
    \bln@ifempty{#1}{#3}%
          {\bln@scan #1,\bln@to {#2}{#3}}%
}

\newcommand*{\ANDL}[1]{\bln@eval {#1}{\bln@and}{\bln@true}}
\newcommand*{\ORL} [1]{\bln@eval {#1}{\bln@or}{\bln@false}}

%% In some contexts, shorter names may be more convenient

\newcommand*{\ShorterBooleanNames}{%
  \let \true  \bln@true
  \let \false \bln@true
  \let \or    \bln@or
  \let \and   \bln@and
  \let \not   \bln@not
  \let \orl   \bln@orl
  \let \andl  \bln@andl
}

%% Comparisson between strings

\newcommand {\stringmatch}[3]{%
   \def \@match ##1#2##2\str@sep ##3\str@sep ##4\str@end
        {#1{##1}{##2}{\ifequalbool{#2}{##3}}}%
   \@match #3\str@sep #2\str@sep \str@sep \str@end
}
\let \str@match \stringmatch

%% The following commands takes in order Prefix Infix Suffix Left

\def \str@infix  #1#2#3{#3}
\def \str@prefix #1#2#3{\AND{#3}{\ifemptybool{#1}}}
\def \str@suffix #1#2#3{\AND{#3}{\ifemptybool{#2}}}

\newcommand {\ifstringinfix} {\str@match {\str@infix}}
\newcommand {\ifstringsuffix}{\str@match {\str@suffix}}
\newcommand {\ifstringprefix}{\str@match {\str@prefix}}

%%%%%%%%%%% From ifjobname Package %%%%%%%%%%%%%

\edef \@jobname {\jobname}

%% Simple equality
\def \job@eattwo #1#2{}
\def \ifjobname #1{%
   \edef \job@tmp {\expandafter \string \csname x#1\endcsname}%
   \edef \job@name {\expandafter \job@eattwo \job@tmp}%
   \ifxbool{\@jobname}{\job@name}%
}

%%

\newtoks \job@temptoken

\def \ifjob@iffix #1#2{%
   \edef \@tmp {\expandafter \string \csname x#2\endcsname}%
   \edef \job@name {\expandafter \job@eattwo \@tmp}%
   \job@temptoken{\def \@match ##1}%
   \@temptokena={##2\relax ##3\relax
           {\def \tmp@prefix {##1}\def \tmp@suffix {##2}\def \tmp@left
           {##3}}}%
   \let \arg@one \job@name%
   \edef \@tmp{\the \job@temptoken\job@name\the \@temptokena}\@tmp
   \edef \@tmp{\noexpand\@match \@jobname \relax \job@name \relax \relax}\@tmp
   #1}

\def \job@infix {\ifxbool{\tmp@left}{\arg@one}}
\def \job@suffix
     {\AND{\ifxbool{\tmp@left}{\arg@one}}{\ifxbool{\empty}{\tmp@suffix}}}
\def \job@prefix
     {\AND{\ifxbool{\tmp@left}{\arg@one}}{\ifxbool{\empty}{\tmp@prefix}}}

\newcommand {\ifjobinfix} {\ifjob@iffix {\job@infix}}
\newcommand {\ifjobsuffix}{\ifjob@iffix {\job@suffix}}
\newcommand {\ifjobprefix}{\ifjob@iffix {\job@prefix}}

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

\ProcessOptions
\LoadClass[fleqn,twoside]{article}%

\usepackage{dashrule}


\usepackage{etex}%
%\reserveinserts{28}% included etex package and \reserveinserts{...} to avoid no room for \newinserts, \newcounts or \newskips etc.
\RequirePackage{graphicx,multicol}%
\RequirePackage{multirow}%
\RequirePackage{cuted}%
\RequirePackage{ifpdf}%
\RequirePackage{url}\def\UrlFont{}%\urlstyle{sf}
\RequirePackage[strict]{changepage}%
\RequirePackage{calc}%
\RequirePackage{xcolor}%
\RequirePackage{float}%
\RequirePackage{floatpag}%
\floatpagestyle{headings}%
\rotfloatpagestyle{headings}%modified from plain to headings to get header and footer for sidewaystable%
\RequirePackage{dblfloatfix}%
\RequirePackage[figuresright]{rotating}%
\RequirePackage{ulem}%
\RequirePackage{enumerate}%
\RequirePackage{soul}
%\usepackage{caption}
\usepackage{lettersp}
\usepackage[english]{babel}
%%
\RequirePackage{hyperref}
\hypersetup{bookmarksopen=true,colorlinks,allcolors=blue,%
            bookmarksdepth=3}%
\RequirePackage{bookmark}% if bookmark package is commented bookmarks are getting printed at the beginning of the file in the output

\RequirePackage[labelformat=empty]{subfig}%

\RequirePackage{twoopt}%
\RequirePackage[bottom]{footmisc}% to place footnotes below floats

\AtBeginDocument{\if@dblspace\usepackage{setspace}\doublespacing\fi}%

\RequirePackage{tabularx}%%  COMPULSARILY HAS TO BE INPUTTED TO USE \tabpara% XMT: Update

\RequirePackage{xargs}
\RequirePackage{tcolorbox}
%%%%%%%%%%%%%%%%%%%%%boxtext%%%%%%%%%%%%%%%%%%%%%

\newenvironment{boxtext}[1]{%
  \fboxsep12\@p@t
  \fboxrule0.5\@p@t
  \vskip12\@p@t
  \begin{breakbox}
    \noindent{\normalsize #1\par}
  \end{breakbox}%
  \vskip12\@p@t%
}

\newenvironment{boxwithhead}[2]{%
  \fboxsep12\@p@t
  \fboxrule0.5\@p@t
  \vskip12\@p@t
  \begin{breakbox}
    {\noindent{\fontsize{9pt}{11.5pt}\selectfont\bfseries{{#1}}}} \vskip-1\@p@t
    \noindent\rule{\linewidth}{0.5\@p@t}\vskip4\@p@t
    \noindent {\noindent\normalsize #2\par}
  \end{breakbox}%
  \vskip12\@p@t%
}

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

%% ========================================================= %%
%% If array package is included before dcolumn package,      %%
%% array environment inside eqnarray throw errors            %%
%% array package is included in dcolumn package; do not      %%
%% include array.sty again in Wileyarticle.cls anywhere;     %%
%% ========================================================= %%
\RequirePackage{varwidth}%
\RequirePackage{dcolumn}%
%
%% Fonts Used
\usepackage[T1]{fontenc}
\usepackage{stix2}
%0
%\usepackage{mathptmx}
%\usepackage{fontspec}%
%\setmainfont[
 % Path=./Fonts/Stix/,
  %UprightFont=*-Regular,
  %BoldFont=*-Bold,
  %ItalicFont=*-Italic,
  %BoldItalicFont=*-BoldItalic,
%]{STIX}%
%\usepackage[defaultmathsizes,italic]{mathastext}

\renewcommand\figurename{\letterspace to 1.26\naturalwidth{{\bf FIGURE}}}%%
\renewcommand\tablename{\letterspace to 1.26\naturalwidth{\bf TABLE}} %%

\renewcommand{\thefigure}{\arabic{figure}}
\renewcommand{\thetable}{\arabic{table}}

\usepackage[letterspace=210,expansion=false]{microtype}

\def\fnum@figure{{\bf\MakeUppercase{F\kern1.5\@p@t I\kern1.5\@p@t G\kern1.5\@p@t U\kern1.5\@p@t R\kern1.5\@p@t E\kern1.5\@p@t}}~\textbf{\thefigure}\hspace{6pt}|\hskip -2\@p@t}
\def\fnum@table{{\bf\MakeUppercase{T\kern1.5\@p@t A\kern1.5\@p@t B\kern1.5\@p@t L\kern1.8\@p@t E\kern1.5\@p@t}}~\thetable\hspace{6pt}|\hskip -2\@p@t}%

\long\def\@makecaption#1#2{%
  \vskip3\@p@t%
  \sbox\@tempboxa{\fontsize{8}{10}\selectfont{\bf #1}\quad #2}%
  \ifdim \wd\@tempboxa >\hsize%
    \fontsize{8}{10}\selectfont{#1}\quad \fontsize{8}{10}\selectfont{#2}\par%
  \else%
    \global \@minipagefalse%
    \hb@xt@\hsize{\box\@tempboxa}%
  \fi%
  \vskip4\@p@t
}

%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%% Layout Settings %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

\RequirePackage[
    %showframe=true,
    %twocolumn=false,%true,
    paperwidth=210mm,
    paperheight=276mm,
    left=16mm,
    right=16mm,
    top=12mm,
    bottom=30mm,%18
    columnsep=6mm,
    headheight=0\@p@t,
    headsep=0\@p@t,
    %marginparsep=0\@p@t,
    %marginparwidth=0pc,
    %bindingoffset=0mm,
    %footskip=48.5\@p@t,
    %centering,
]{geometry}


%
\newdimen\parindentvalue%
\parindentvalue=1em%
%
\setlength\parindent{\parindentvalue}%
%
\ifpdf%
    \global\@cropmarksonfalse%
\else%
    \if@cropmarkson%
        \RequirePackage{crop}%
        \if@croppage%
            \CROP@size{\paperwidth}{\paperheight}%
        \fi%
    \fi%
\fi%

%
%%%%%%%%%%%%%%%%%%%%%%%%%%%% Pagination Settings %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
% Line spacing
\setlength\lineskip{0\@p@t}%
\setlength\normallineskip{0\@p@t}%
\renewcommand\baselinestretch{}%
\parskip=\z@%

\pagenumbering{arabic}%
\sloppy%
\frenchspacing%

%%% special parameters for TeX
\adjdemerits=100
\linepenalty=100


% Page break penalties
\@lowpenalty   51
\@medpenalty  151
\@highpenalty 301

% Disallow widows and orphans
\clubpenalty 10000
\widowpenalty 10000

% Disable page breaks before equations, allow pagebreaks after
% equations and discourage widow lines before equations.
\displaywidowpenalty 100
\predisplaypenalty   10000
\postdisplaypenalty  0

% Set these global demerits
\doublehyphendemerits 1000000% corresponds to badness 800
\finalhyphendemerits  1000000% corresponds to badness 1000

% Allow loose lines rather than overfull lines
\vbadness=9999%
\tolerance=9999%


% Allow breaking the page in the middle of a paragraph
\interlinepenalty 0%

% Disallow breaking the page after a hyphenated line
\brokenpenalty 10000%

% Hyphenation; don't split words into less than three characters
\lefthyphenmin=3%
\righthyphenmin=3%

% Float placement parameters

% The total number of floats that can be allowed on a page.
\setcounter{totalnumber}{10}%

% The maximum number of floats at the top and bottom of a page.
\setcounter{topnumber}{3}%
\setcounter{bottomnumber}{3}%

% The maximum part of the top or bottom of a text page that can be
% occupied by floats. This is set so that at least four lines of text
% fit on the page.
\renewcommand\topfraction{.921}%
\renewcommand\bottomfraction{.921}%

% The minimum amount of a text page that must be occupied by text.
% This should accomodate four lines of text.
%%\renewcommand\textfraction{.13}%
\renewcommand\textfraction{.1}%

% The minimum amount of a float page that must be occupied by floats.
%%\renewcommand\floatpagefraction{.887}%
\renewcommand\floatpagefraction{.95}%

% The same parameters repeated for double column output
%%\renewcommand\dbltopfraction{.88}%
%%\renewcommand\dblfloatpagefraction{.88}%
\renewcommand\dbltopfraction{.95}%
\renewcommand\dblfloatpagefraction{.95}%

% Space between floats
\setlength\floatsep{24\@p@t \@plus 2.875\@p@t \@minus 1.4375\@p@t}%

% Space between floats and text
\setlength\textfloatsep{24\@p@t \@plus 2.875\@p@t \@minus 1.4375\@p@t}%
%
%%\def\resettextfloatsep{\makeatletter\setlength\textfloatsep{13.5\@p@t \@plus 2.875\@p@t \@minus 1.4375\@p@t}\makeatother}%
%
% Space above and below an inline figure
\setlength\intextsep{24\@p@t \@plus 2.875\@p@t \@minus 1.4375\@p@t}%

% For double column floats
\setlength\dblfloatsep{24\@p@t \@plus 2.875\@p@t \@minus 1.4375\@p@t}%
\setlength\dbltextfloatsep{24\@p@t \@plus 2.875\@p@t \@minus 1.4375\@p@t}%

\hyphenation{Figure Figures Table Tables Equation Equations Section Sections Appendix Theorem Lemma Proposition Corollary}%
\hyphenation{Claim Conjecture Property Remark Problem Example Definition Algorithm Assumption Axiom Case Hypotheses Note Proof}%

%%% miscellaneous macros %%%
\def\zz#1{\vphantom{#1}}%
\def\nobreaktie{\penalty\@M\hbox{\ }} % tie

%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% Draftrules %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%%% To print grid lines on all pages
\newdimen\pageheight\global\pageheight=\textheight%
\newcount\rulecount\rulecount=1%
%
\newdimen\leftcolrulepos
\newdimen\rightcolrulepos
%
\edef\slugtoday{\two@digits{\the\day}/\two@digits{\the\month}/\the\year}%
%
\def\PrintGridLines{%
{\color{black!10}%
    {%
    \loop%
    \ifdim\pageheight>0\@p@t%
        \ifnum\rulecount=1%
            \advance\pageheight by -1.5\@p@t%
        \fi%
        \AtTextUpperLeft{%x
          \put(0,\LenToUnit{-\pageheight}){%
            \line(1,0){\LenToUnit{\leftcolrulepos}}}}%
%%14Mar16%%
        \AtTextUpperLeft{%
        \put(\LenToUnit{\rightcolrulepos},\LenToUnit{-\pageheight}){%
           \line(1,0){\LenToUnit{\leftcolrulepos}}}}%
        \advance\pageheight-11.5\@p@t%baseline
        \advance\rulecount\@ne%
    \repeat%
}}}%
%
%\RequirePackage{showframe}

%% Miscellaneous macros %%

\newdimen\artopleftskipdim%
\artopleftskipdim=36.25\@p@t%

\def\hb{\hfill\break}%
\def\uns{\ifmmode\,\else$\,$\fi}%
\def\raggedcenter{\leftskip=0\@p@t plus 0.5fil\rightskip=0\@p@t plus 0.5fil\parfillskip=0\@p@t}%
\def\artopraggedright{\leftskip=\artopleftskipdim\rightskip=0\@p@t plus 1fil\parfillskip=0\@p@t\relax}%

%% Metadata Info %%

\def\@artid{xx}% pub-id
\def\@jname{xxx}%
\def\@jvol{\emph{xxx}}%
\def\@eissn{xx}%
\def\@pissn{xx}%
%%\def\@copyyear{xxxx}%
\def\copyyr{xxxx}%
\def\@jnlyear{xxxx}%
\def\@doi{}%
\def\@pubname{ }%
\def\@arteditor{}%

\newcommand{\articledoi}[1]{\newcommand{\thearticledoi}{#1}}

\def\artid#1{\gdef\@artid{#1}}%
\def\@jid{}%
\def\jid#1{\gdef\@jid{#1}}%
\def\jname#1{\gdef\@jname{#1}}%
\def\jvol#1{\gdef\@jvol{#1}}%
\def\eissn#1{\gdef\@eissn{#1}}%
\def\pissn#1{\gdef\@pissn{#1}}%
\def\copyyear#1{\gdef\copyyr{#1}}%
\def\jyear#1{%%\gdef\copyyear{#1}
\gdef\@jnlyear{#1}}%
%\def\doi#1{\gdef\@doi{#1}}%

\def\pubname#1{\gdef\@pubname{#1}}%

\def\jmonth#1{\gdef\@jmonth{#1}}%


\def\@copyeditor{}%
\def\copyeditor#1{\gdef\@copyeditor{#1}}%

\def\orcid[#1]#2#3{}%
\newcommand{\orgdiv}[1]{#1}%
%\def\orgdiv#1{\gdef\@orgdiv{#1}}%
\newcommand{\orgname}[1]{#1}%
\newcommand{\orgaddress}[1]{#1}%
\newcommand{\street}[1]{#1}%
\newcommand{\postcode}[1]{#1}%
\newcommand{\city}[1]{#1}%
\newcommand{\state}[1]{#1}%
\newcommand{\country}[1]{#1}%

\def\licenseVal#1{{}}%

\def\@journal{}%
\def\@volume{}%
\def\@copyright{}%
\gdef\journal#1{\gdef\@journal{#1}}%
\gdef\volume#1{\gdef\@volume{#1}}%
\gdef\copyright#1{\gdef\@copyright{#1}}%

\def\@copyyear{}%
\gdef\copyyear#1{\gdef\@copyyear{#1}}%
\newcommand\copyrightinfo[2][]{\if!#2!\gdef\@copyyear{}\else\gdef\@copyyear{\copyrightfont#2\ \@licenseVal}\fi}%

%% Macros for firstpage %%

\def\startpage#1{%
  \gdef\temp{#1}%
  \ifx\temp\@empty%
    \def\@firstpage{1}%
  \else%
    \def\@firstpage{#1}%
    \setcounter{page}{#1}%
    \setcounter{spagecounter}{#1}%
  \fi%
}%
\def\@firstpage{}




\def\endpage#1{\gdef\@endpage{#1}}
\def\@endpage{}

%
%% Cross Link for Author & Address
%
\def\jmkLabel#1{\@bsphack\protected@write\@auxout{}{\string\Newlabel{#1}{\@currentlabel}}\@esphack}%
\def\Newlabel#1#2{\expandafter\xdef\csname X@#1\endcsname{#2}}%
\def\jmkRef#1{\@ifundefined{X@#1}{0}{\csname X@#1\endcsname}}%

%
%%%%%%%%%%%%%%%%%% Updation for xml conversion
%
\gdef\@FirstPg{}%
\gdef\@LastPg{}%
%
\def\FirstPg#1{\if!#1!\else\gdef\@FirstPg{#1}\fi}%
\def\LastPg#1{\if!#1!\else\gdef\@LastPg{#1}\fi}%

%\newcount\LastAQCnt%

\newwrite\@xmlpage
\openout\@xmlpage=\jobname.pag
\newcounter{spagecounter}
\newcounter{lpagecounter}
%%\AtBeginDocument{\setcounter{spagecounter}{\c@page}}
\AtEndDocument{\setcounter{lpagecounter}{\c@page}%
\addtocounter{lpagecounter}{-1}%
\immediate\write\@xmlpage{\string\FirstPg{\thespagecounter}\string\LastPg{\thelpagecounter}}%
\addtocounter{lpagecounter}{1}%
\immediate\write\@auxout{\string\FirstPg{\thespagecounter}\string\LastPg{\thelpagecounter}}%
\immediate\closeout\@xmlpage%
}%

%% Fonts definitions %%

  \def\footnotetextfont{\rmfamily\fontsize{8\@p@t}{10\@p@t}\selectfont}%
  %%
  \def\footnotetextfont{\rmfamily\fontsize{6\@p@t}{8\@p@t}\selectfont}



%\usepackage{microtype}
%\DisableLigatures[f]{encoding = *, family = * }

\newcommand{\sbdefault}{sb}
\DeclareRobustCommand\sbseries{\not@math@alphabet\sbseries\mathbf\fontseries\sbdefault\selectfont}
\DeclareTextFontCommand{\textsb}{\sbseries}
\DeclareRobustCommand\sfitseries{\not@math@alphabet\sfitseries\normalfont\fontseries{m}\fontshape{it}\selectfont}
\DeclareTextFontCommand{\textsfi}{\sfitseries}
\DeclareOldFontCommand{\rm}{\normalfont\rmfamily}{\mathrm}
\DeclareOldFontCommand{\sf}{\normalfont\sffamily}{\mathsf}
\DeclareOldFontCommand{\tt}{\normalfont\ttfamily}{\mathtt}
\DeclareOldFontCommand{\bf}{\normalfont\bfseries}{\mathbf}
\DeclareOldFontCommand{\it}{\normalfont\itshape}{\mathit}
\DeclareOldFontCommand{\em}{\normalfont\itshape}{\it}
\DeclareOldFontCommand{\sl}{\normalfont\slshape}{\@nomath\sl}
\DeclareOldFontCommand{\sc}{\normalfont\scshape}{\@nomath\sc}
\DeclareRobustCommand*\cal{\@fontswitch\relax\mathcal}
\DeclareRobustCommand*\mit{\@fontswitch\relax\mathnormal}
\if@exactptsize%
  \def\set@fontsize#1#2#3{%
    \@defaultunits\@tempdimb#2\@p@t\relax\@nnil%
    \edef\f@size{\strip@pt\@tempdimb}%
    \@defaultunits\@tempskipa#3\@p@t\relax\@nnil%
    \edef\f@baselineskip{\the\@tempskipa}%
    \edef\f@linespread{#1}%
    \let\baselinestretch\f@linespread%
    \def\size@update{%
      \baselineskip\f@baselineskip\relax%
      \baselineskip\f@linespread\baselineskip%
      \normalbaselineskip\baselineskip%
      \setbox\strutbox\hbox{\vrule\@height.7\baselineskip\@depth.3\baselineskip\@width\z@}%
      \let\size@update\relax%
    }%
  }%
\fi%

\if@twocolumn
\renewcommand\normalsize{%
  \@setfontsize\normalsize{9\@p@t}{11.5\@p@t}%
  \abovedisplayskip 10\@p@t \@plus2\@p@t \@minus0\@p@t
  \abovedisplayshortskip \z@ \@plus3\@p@t%
  \belowdisplayshortskip 5\@p@t \@plus3\@p@t \@minus3\@p@t%
  \belowdisplayskip \abovedisplayskip%
  \let\@listi\@listI%
}%
\newcommand{\textbaselineskip}{\baselineskip13\@p@t}%
\normalsize\baselineskip13\@p@t%
\newcommand\medsize{%
  \@setfontsize\small\@viipt{9}%
  \abovedisplayskip 5\@p@t \@plus3\@p@t \@minus4\@p@t
  \abovedisplayshortskip \z@ \@plus2\@p@t
  \belowdisplayshortskip 3\@p@t \@plus2\@p@t \@minus2\@p@t
  \def\@listi{%
    \leftmargin\leftmargini
    \topsep 4\@p@t \@plus2\@p@t \@minus2\@p@t
    \parsep 2\@p@t \@plus\p@ \@minus\p@
    \itemsep \parsep}%
  \belowdisplayskip\abovedisplayskip}
\renewcommand\small{%
  \@setfontsize\small\@vpt{7}%
  \abovedisplayskip 5\@p@t \@plus3\@p@t \@minus4\@p@t
  \abovedisplayshortskip \z@ \@plus2\@p@t
  \belowdisplayshortskip 3\@p@t \@plus2\@p@t \@minus2\@p@t
  \def\@listi{%
    \leftmargin\leftmargini
    \topsep 4\@p@t \@plus2\@p@t \@minus2\@p@t
    \parsep 2\@p@t \@plus\p@ \@minus\p@
    \itemsep \parsep}%
  \belowdisplayskip \abovedisplayskip}
\renewcommand\footnotesize{%
  \@setfontsize\footnotesize{7}{9}%
  \abovedisplayskip 5\@p@t \@plus2\@p@t \@minus4\@p@t
  \abovedisplayshortskip \z@ \@plus\p@
  \belowdisplayshortskip 3\@p@t \@plus\p@ \@minus2\@p@t
  \def\@listi{%
    \leftmargin\leftmargini
    \topsep 3\@p@t \@plus\p@ \@minus\p@
    \parsep 2\@p@t \@plus\p@ \@minus\p@
    \itemsep \parsep}%
  \belowdisplayskip \abovedisplayskip}
\newcommand\refapasize{\@setfontsize\refapasize{8.5}{11}}
\renewcommand\scriptsize{\@setfontsize\scriptsize\@ixpt\@ixpt}
\newcommand\scrisize{\@setfontsize\scrisize{9.3}{9}}
\renewcommand\tiny{\@setfontsize\tiny\@vpt\@vipt}
\renewcommand\large{\@setfontsize\large{12}{14}}
\newcommand\larg{\@setfontsize\larg{11}{13}}
\renewcommand\Large{\@setfontsize\Large{16}{18}}
\newcommand\fpe{\@setfontsize\fpe{19}{21}}
\renewcommand\LARGE{\@setfontsize\LARGE\@xviipt{22}}
\newcommand\rke{\@setfontsize\rke{10}{10}}
\newcommand\ke{\@setfontsize\ke{9}{10}}
\newcommand\lke{\@setfontsize\lke{8.4}{8.25}}
\newcommand\ffe{\@setfontsize\ffe{9}{12}}
\newcommand\adf{\@setfontsize\adf{10.3}{12}}
\newcommand\kes{\@setfontsize\kes{8}{9.5}}
\newcommand\bis{\@setfontsize\bis{9}{10}}
\newcommand\tdf{\@setfontsize\tdf{14}{16}}
\newcommand\svo{\@setfontsize\svo{14.5}{16}}
\renewcommand\huge{\@setfontsize\huge\@xxpt{25}}
\renewcommand\Huge{\@setfontsize\Huge\@xxvpt{30}}
\else
\renewcommand\normalsize{%
  \@setfontsize\normalsize{10\@p@t}{12.5\@p@t}%
  \abovedisplayskip 10\@p@t \@plus2\@p@t \@minus0\@p@t
  \abovedisplayshortskip \z@ \@plus3\@p@t%
  \belowdisplayshortskip 5\@p@t \@plus3\@p@t \@minus3\@p@t%
  \belowdisplayskip \abovedisplayskip%
  \let\@listi\@listI%
}%
\newcommand{\textbaselineskip}{\baselineskip14\@p@t}%
\normalsize\baselineskip14\@p@t%
\newcommand\medsize{%
  \@setfontsize\small\@viipt{9}%
  \abovedisplayskip 5\@p@t \@plus3\@p@t \@minus4\@p@t
  \abovedisplayshortskip \z@ \@plus2\@p@t
  \belowdisplayshortskip 3\@p@t \@plus2\@p@t \@minus2\@p@t
  \def\@listi{%
    \leftmargin\leftmargini
    \topsep 4\@p@t \@plus2\@p@t \@minus2\@p@t
    \parsep 2\@p@t \@plus\p@ \@minus\p@
    \itemsep \parsep}%
  \belowdisplayskip\abovedisplayskip}
\renewcommand\small{%
  \@setfontsize\small\@vipt{8}%
  \abovedisplayskip 5\@p@t \@plus3\@p@t \@minus4\@p@t
  \abovedisplayshortskip \z@ \@plus2\@p@t
  \belowdisplayshortskip 3\@p@t \@plus2\@p@t \@minus2\@p@t
  \def\@listi{%
    \leftmargin\leftmargini
    \topsep 4\@p@t \@plus2\@p@t \@minus2\@p@t
    \parsep 2\@p@t \@plus\p@ \@minus\p@
    \itemsep \parsep}%
  \belowdisplayskip \abovedisplayskip}
\renewcommand\footnotesize{%
  \@setfontsize\footnotesize{8}{10}%
  \abovedisplayskip 5\@p@t \@plus2\@p@t \@minus4\@p@t
  \abovedisplayshortskip \z@ \@plus\p@
  \belowdisplayshortskip 3\@p@t \@plus\p@ \@minus2\@p@t
  \def\@listi{%
    \leftmargin\leftmargini
    \topsep 3\@p@t \@plus\p@ \@minus\p@
    \parsep 2\@p@t \@plus\p@ \@minus\p@
    \itemsep \parsep}%
  \belowdisplayskip \abovedisplayskip}
\newcommand\refapasize{\@setfontsize\refapasize{8.5}{11}}
\renewcommand\scriptsize{\@setfontsize\scriptsize\@ixpt\@ixpt}
\newcommand\scrisize{\@setfontsize\scrisize{9.3}{9}}
\renewcommand\tiny{\@setfontsize\tiny\@vpt\@vipt}
\renewcommand\large{\@setfontsize\large{12}{14}}
\newcommand\larg{\@setfontsize\larg{11}{13}}
\renewcommand\Large{\@setfontsize\Large{16}{18}}
\newcommand\fpe{\@setfontsize\fpe{19}{21}}
\renewcommand\LARGE{\@setfontsize\LARGE\@xviipt{22}}
\newcommand\rke{\@setfontsize\rke{10}{10}}
\newcommand\ke{\@setfontsize\ke{9}{10}}
\newcommand\lke{\@setfontsize\lke{8.4}{8.25}}
\newcommand\ffe{\@setfontsize\ffe{9}{12}}
\newcommand\adf{\@setfontsize\adf{10.3}{12}}
\newcommand\kes{\@setfontsize\kes{8}{9.5}}
\newcommand\bis{\@setfontsize\bis{9}{10}}
\newcommand\tdf{\@setfontsize\tdf{14}{16}}
\newcommand\svo{\@setfontsize\svo{14.5}{16}}
\renewcommand\huge{\@setfontsize\huge\@xxpt{25}}
\renewcommand\Huge{\@setfontsize\Huge\@xxvpt{30}}
\fi

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%% FONT DECLARATION Ends %%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%



%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%% FONTS / COLOR DECLARATION Starts %%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%%
\def\dummyalphabet{\vphantom{abcdefghijklmnopqrstuvwxyz0123456789}}
%
\sodef\letterspacetwty{}{.2em}{.4em plus1em}{.5em plus 1em minus1em }
%
\definecolor{Black}{gray}{0}
\definecolor{White}{cmyk}{0.0,0.0,0.0,0.0}
\definecolor{Tcolor}{cmyk}{1,0.8,0.0,0.0}%
\definecolor{Jcolor}{cmyk}{0.0,0.85,1,0.0}%
\definecolor{rgray}{cmyk}{0.0,0.0,0.0,0.4}%
\definecolor{lightgray}{cmyk}{0.0,0.0,0.0,0.2}
\definecolor{Blue}{cmyk}{1,0.77,0.0,0.0}
\definecolor{AbstractBG}{cmyk}{0.0,0.0,0.0,0.1}
%

\if@twocolumn
\def\@journalfont{\rmfamily\fontsize{10}{11.5}\bfseries\itshape\selectfont}%
  %
  \def\captionfont{\fontsize{8.5}{12}\selectfont}
  %
  \def\pagenumfont{\rmfamily\fontsize{7}{9}\selectfont}%
  \def\cnmpagenumfont{\rmfamily\fontsize{7}{9}\selectfont\bfseries}%
  \def\runningheadfont{\rmfamily\fontsize{7}{9}\selectfont}%New updations 19aug2016
  \def\runningfootfont{\rmfamily\fontsize{7}{9}\selectfont}%
  \def\titlepageheadfont{\rmfamily\fontsize{7}{9}\selectfont}%
  %
  \def\BRarttypefont{\reset@font\rmfamily\fontsize{18}{18}\fontseries{b}\selectfont}%
  \def\pubheadfont{\reset@font\rmfamily\fontsize{7}{9}\fontseries{b}\selectfont}%
  \def\arttypefont{\rmfamily\fontsize{10}{10}\selectfont}%
  \def\copyrightfont{\rmfamily\fontsize{7}{9}\selectfont}%
  \def\SParttypefont{\rmfamily\fontsize{9}{12}\fontseries{b}\selectfont}%
  \def\titlefont{\rmfamily\fontsize{18.0}{21}\selectfont\bfseries\leftskip\z@\rightskip\z@ plus1fil}%
  \def\subtitlefont{\rmfamily\fontsize{16}{21}\selectfont\bf\leftskip\z@\rightskip\z@ plus1fil}%
  \def\Authorfont{\rmfamily\fontsize{9}{12}\selectfont\leftskip\z@\rightskip\z@ plus1fil}%
  \def\absheadfont{\rmfamily\fontsize{10}{15}\fontseries{b}\selectfont\bfseries}%
  \def\legalstatementfont{\rmfamily\fontsize{7}{10}\selectfont\leftskip0\@p@t\rightskip\leftskip}%
  \def\BRsectionfont{\rmfamily\fontsize{10}{16}\fontseries{b}\selectfont\leftskip\z@\rightskip\z@ plus1fil}%
  \def\sectionfont{\rmfamily\fontsize{12}{13}\fontseries{b}\selectfont\leftskip\z@\rightskip\z@ plus1fil}%
  \def\subsectionfont{\rmfamily\fontsize{12}{13}\bfseries\selectfont\leftskip\z@\rightskip\z@ plus1fil}%
  \def\subsubsectionfont{\rmfamily\fontsize{12}{13}\selectfont\bfseries\leftskip\z@\rightskip\z@ plus1fil}%
  \def\paragraphfont{\rmfamily\fontsize{10.5}{13}\selectfont\bfseries}%
  \def\subparagraphfont{\rmfamily\fontsize{10}{13}\selectfont\bfseries}%
  \def\appsectionfont{\rmfamily\fontsize{10}{13}\fontseries{b}\selectfont\leftskip\z@\rightskip\z@ plus1fil}%
  %
  \def\boxheadfont{\rmfamily\fontsize{10}{13}\fontseries{b}\selectfont}
  \def\boxtitlefont{\rmfamily\fontsize{10}{13}\bfseries\selectfont}
  %
  \def\GnSabsfont{\rmfamily\fontsize{9}{15}\selectfont}%
  \def\GnSabsfootfont{\reset@font\rmfamily\fontsize{14}{0}\bfseries\selectfont}%
  %
  \def\suppinfofont{\noindent\rmfamily}%
  \def\suppinfoheadfont{\noindent\fontsize{10}{13}\fontseries{b}\selectfont}%
  \def\suppinfocaptionfont{\noindent\rmfamily}%
  %
  \def\figurenumfont{\rmfamily\fontsize{8.5\@p@t}{12}\fontseries{b}\selectfont}%
  \def\figurecaptionfont{\rmfamily\fontsize{8.5\@p@t}{12}\selectfont}
  \def\bwfiginfofont{\fontfamily{tim}\fontsize{10\@p@t}{10\@p@t}\selectfont}%
  %
  \def\tablenumfont{\rmfamily\fontsize{9\@p@t}{11.5\@p@t}\fontseries{b}\selectfont}%
  \def\keypointheadfont{\reset@font\rmfamily\fontsize{10\@p@t}{13\@p@t}\fontseries{b}\selectfont}%
  \def\tablecaptionfont{\rmfamily\fontsize{8.5\@p@t}{12\@p@t}\selectfont}
  \def\tablebodyfont{\rmfamily\fontsize{8.5\@p@t}{11.5\@p@t}\selectfont}
  \def\tablecolheadfont{\rmfamily\fontsize{8.5\@p@t}{11.5\@p@t}\selectfont\bfseries}
  \def\tablefootnotefont{\rmfamily\fontsize{7.5\@p@t}{10.5\@p@t}\selectfont}
  %
  \def\footnotetextfont{\rmfamily\fontsize{7\@p@t}{9\@p@t}\selectfont}
  %
  \def\listfont{\normalsize}%
  %
  \def\refheadfont{\fontsize{10}{13}\fontseries{b}\selectfont}
  \def\reflistfont{\fontsize{8.5}{11.5}\selectfont}
  %
  \def\biographyheadfont{\fontsize{9pt}{11.5pt}\fontseries{b}\selectfont}
  %
  \def\ackheadfont{\fontsize{10}{13}\selectfont\fontseries{b}\selectfont}
  \def\addressfont{\rmfamily\fontsize{7.5}{10}\selectfont\leftskip\z@\rightskip\z@}%
  \def\corresfont{\rmfamily\fontsize{7.5}{10}\selectfont\leftskip\z@\rightskip\z@ plus1fil}%
  \def\FIfont{\rmfamily\fontsize{7.5}{10}\selectfont}%
  \def\JELfont{\rmfamily\fontsize{7.5}{10}\selectfont\leftskip\z@\rightskip\z@ plus1fil}%
  %\def\keywordsheadfont{\hsize\abs@colii@hsize\rmfamily\fontsize{7.5}{10}\selectfont\ifAbstractexist\leftskip7\@p@t\rightskip\leftskip\fi}%
  \def\abstractfont{\rmfamily\fontsize{9}{13}\selectfont}%
  %\def\keywordsfont{\rmfamily\fontsize{7.5}{13}\selectfont\ifAbstractexist\leftskip7\@p@t\rightskip\leftskip\fi}%
  %

\def\listfont{\reset@font\fontfamily{\rmdefault}\normalsize}%
\def\tablistfont{\tablebodyfont}%
%%%
\else
%%%
\def\@journalfont{\rmfamily\fontsize{10}{11.5}\bfseries\itshape\selectfont}%
  %
  \def\captionfont{\fontsize{8.5}{12}\selectfont}
  %
  \def\pagenumfont{\rmfamily\fontsize{7}{9}\selectfont}%
  \def\cnmpagenumfont{\rmfamily\fontsize{7}{9}\selectfont\bfseries}%
  \def\runningheadfont{\rmfamily\fontsize{7}{9}\selectfont}%New updations 19aug2016
  \def\runningfootfont{\rmfamily\fontsize{7}{9}\selectfont}%
  \def\titlepageheadfont{\rmfamily\fontsize{7}{9}\selectfont}%
  %
  \def\BRarttypefont{\reset@font\rmfamily\fontsize{18}{18}\fontseries{b}\selectfont}%
  \def\pubheadfont{\reset@font\rmfamily\fontsize{7}{9}\fontseries{b}\selectfont}%
  \def\arttypefont{\rmfamily\fontsize{10}{10}\selectfont}%
  \def\copyrightfont{\rmfamily\fontsize{7}{9}\selectfont}%
  \def\SParttypefont{\rmfamily\fontsize{9}{12}\fontseries{b}\selectfont}%
  \def\titlefont{\rmfamily\fontsize{19.0}{22}\selectfont\bfseries\leftskip\z@\rightskip\z@ plus1fil}%
  \def\subtitlefont{\rmfamily\fontsize{17}{22}\selectfont\bf\leftskip\z@\rightskip\z@ plus1fil}%
  \def\Authorfont{\rmfamily\fontsize{10}{12}\selectfont\leftskip\z@\rightskip\z@ plus1fil}%
  \def\absheadfont{\rmfamily\fontsize{11}{15}\fontseries{b}\selectfont\bfseries}%
  \def\legalstatementfont{\rmfamily\fontsize{8}{10}\selectfont\leftskip0\@p@t\rightskip\leftskip}%
  \def\BRsectionfont{\rmfamily\fontsize{11}{16}\fontseries{b}\selectfont\leftskip\z@\rightskip\z@ plus1fil}%
  \def\sectionfont{\rmfamily\fontsize{13}{15.6}\fontseries{b}\selectfont\leftskip\z@\rightskip\z@ plus1fil}%
  \def\subsectionfont{\rmfamily\fontsize{13}{15.6}\bfseries\selectfont\leftskip\z@\rightskip\z@ plus1fil}%
  \def\subsubsectionfont{\rmfamily\fontsize{13}{15.6}\selectfont\bfseries\leftskip\z@\rightskip\z@ plus1fil}%
  \def\paragraphfont{\rmfamily\fontsize{11}{13}\selectfont\bfseries}%
  \def\subparagraphfont{\rmfamily\fontsize{11}{13}\selectfont\bfseries}%
  \def\appsectionfont{\rmfamily\fontsize{11}{13}\fontseries{b}\selectfont\leftskip\z@\rightskip\z@ plus1fil}%
  %
  \def\boxheadfont{\rmfamily\fontsize{11}{13}\fontseries{b}\selectfont}
  \def\boxtitlefont{\rmfamily\fontsize{11}{13}\bfseries\selectfont}
  %
  \def\GnSabsfont{\rmfamily\fontsize{10}{15}\selectfont}%
  \def\GnSabsfootfont{\reset@font\rmfamily\fontsize{14}{0}\bfseries\selectfont}%
  %
  \def\suppinfofont{\noindent\rmfamily}%
  \def\suppinfoheadfont{\noindent\fontsize{11}{13}\fontseries{b}\selectfont}%
  \def\suppinfocaptionfont{\noindent\rmfamily}%
  %
  \def\figurenumfont{\rmfamily\fontsize{9\@p@t}{12}\fontseries{b}\selectfont}%
  \def\figurecaptionfont{\rmfamily\fontsize{9\@p@t}{12}\selectfont}
  \def\bwfiginfofont{\fontfamily{tim}\fontsize{11\@p@t}{10\@p@t}\selectfont}%
  %
  \def\tablenumfont{\rmfamily\fontsize{9\@p@t}{12\@p@t}\fontseries{b}\selectfont}%
  \def\keypointheadfont{\reset@font\rmfamily\fontsize{11\@p@t}{13\@p@t}\fontseries{b}\selectfont}%
  \def\tablecaptionfont{\rmfamily\fontsize{9.5\@p@t}{12\@p@t}\selectfont}
  \def\tablebodyfont{\rmfamily\fontsize{9.5\@p@t}{11.5\@p@t}\selectfont}
  \def\tablecolheadfont{\rmfamily\fontsize{9.5\@p@t}{11.5\@p@t}\selectfont\bfseries}
  \def\tablefootnotefont{\rmfamily\fontsize{8.5\@p@t}{10.5\@p@t}\selectfont}
  %
  \def\footnotetextfont{\rmfamily\fontsize{8\@p@t}{9\@p@t}\selectfont}
  %
  \def\listfont{\normalsize}%
  %
  \def\refheadfont{\fontsize{11}{13}\fontseries{b}\selectfont}
  \def\reflistfont{\fontsize{9.5}{11.5}\selectfont}
  %
  \def\biographyheadfont{\fontsize{10pt}{12pt}\fontseries{b}\selectfont}
  %
  \def\ackheadfont{\fontsize{11}{13}\selectfont\fontseries{b}\selectfont}
  \def\addressfont{\rmfamily\fontsize{8.5}{10}\selectfont\leftskip\z@\rightskip\z@}%
  \def\corresfont{\rmfamily\fontsize{8.5}{10}\selectfont\leftskip\z@\rightskip\z@ plus1fil}%
  \def\FIfont{\rmfamily\fontsize{8.5}{10}\selectfont}%
  \def\JELfont{\rmfamily\fontsize{8.5}{10}\selectfont\leftskip\z@\rightskip\z@ plus1fil}%
  %\def\keywordsheadfont{\hsize\abs@colii@hsize\rmfamily\fontsize{7.5}{10}\selectfont\ifAbstractexist\leftskip7\@p@t\rightskip\leftskip\fi}%
  \def\abstractfont{\rmfamily\fontsize{10}{13}\selectfont}%
  %\def\keywordsfont{\rmfamily\fontsize{8.5}{13}\selectfont\ifAbstractexist\leftskip7\@p@t\rightskip\leftskip\fi}%
  %

\def\listfont{\reset@font\fontfamily{\rmdefault}\normalsize}%
\def\tablistfont{\tablebodyfont}%
\fi

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%% article front matter elements %%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

%% article opening Running headers %%

\def\artoprunningheaders{\vbox{\OPRHfont\raggedright%
\@pubname\par%
\@jname\par%
Volume\ \@jvol, Article ID\ \@artid, \thepage~pages\par%
%%\if@hyperrefon\urlstyle{same}\burlalt{http://dx.doi.org/\@doi}{http://dx.doi.org/\@doi}\else{http://dx.doi.org/\@doi}\fi% this line is not required in the output
}}

\def\nullartoprunningheaders{\vbox{\OPRHfont\raggedright%
\phantom{\@pubname}\par%
\phantom{\@jname}\par%
\phantom{Volume\ \@jvol, Article ID\ \@artid, \thepage pages}\par%
%%\if@hyperrefon\urlstyle{same}\burlalt{http://dx.doi.org/\@doi}{http://dx.doi.org/\@doi}\else{http://dx.doi.org/\@doi}\fi%
}}

%% article category %%

\newcommand\dunderline[3][-1\@p@t]{{%
  \sbox0{#3}%
  \ooalign{\copy0\cr\rule[\dimexpr#1-#2\relax]{\wd0}{#2}}}}

\let\@articletype\@empty%

\def\@@articletype{%
  \rule[-4\@p@t]{4\@p@t}{15\@p@t}%
  \hskip -0\@p@t%
  {%
    \arttypefont\bf%
    \uppercase{\letterspace to 1.15\naturalwidth{\@articletype}}%
  }%
  \hskip -2\@p@t%
  \raisebox{-2\@p@t}{\includegraphics[width=0.70in]{images/openaccess}}%
}%

\def\articletype#1{\gdef\@articletype{#1}}%

%% article title %%

\def\titlemark#1{\gdef\@titlemark{#1}}%
\gdef\@titlemark{}%

%%\def\titlefootnotemark#1{\textsuperscript{\hbox{\reset@font\fontsize{12.5}{0}\selectfont$\@fnsymbol{#1}$}}}%
\def\titlefootnotemark#1{\textsuperscript{$\@fnsymbol{#1}$}}%


\def\title#1{\gdef\@title{\hsize=\textwidth%
\titlefont\bf #1\addcontentsline{toc}{chapter}{#1}% for bookmarks
}}%
\def\shrttitle#1{\gdef\@shrttitle{#1}}\shrttitle{}%

%% article authors %%

\let\sep\@empty%
\let\authorsep\@empty%
\let\emailsep\@empty%

\newcount\corrauthcount%

\def\artauthors{}


\newcommand{\thecorrauthcount}{\the\corrauthcount}%
\newcommand{\thefnmCnt}{\the\fnmCnt}%

\newcount\authfncnt%
\authfncnt=0%

\newcount\authfnTmpCnt%
\authfnTmpCnt=1%
%
\gdef\StoreAuthFnText#1#2{%
\edef\GetRoman{\romannumeral#1}%
\expandafter\gdef\csname\GetRoman StoreAuthFnTxt\endcsname{#2}%
}%
%
\def\authfndefns{%
\loop\ifnum\authfncnt>0%
   \csname\romannumeral\authfnTmpCnt StoreAuthFnTxt\endcsname\par%
   \StepDownCounter{\authfncnt}%
   \StepUpCounter{\authfnTmpCnt}%
\repeat}%

\newcommand\fnm[1]{{\StepUpCounter{\fnmCnt}#1\addtocontents{aq}{\protect{\bfseries Author\ \thefnmCnt}\hfill\protect\break Given Names: #1\hfill\protect\break}}}%
\newcommand\sur[1]{{#1\addtocontents{aq}{Last Name: #1\protect\par}}}%

\newbox\aufootnotemark%

\newbox\authmarkbox%
\newif\ifGApresent%

\def\@filtername#1#2\@nil{#1.\ }%
\def\filtername#1 #2\@nil{%
\@filtername#1\@nil #2}%
\renewcommandx{\author}[4][1=,3=,4=]{%
  \def\temp@one{#1}%
  \g@addto@macro\artauthors{%
    \def\footnotemark##1{%
      \affilmarkdefns%
      \global\setbox\authmarkbox\hbox{%
        \reset@font\fontfamily{\rmdefault}\fontsize{8.75\@p@t}{0}\selectfont{##1}%
      }%
    }%
    \Authorfont%\baselineskip=12\@p@t%
    \def\baselinestretch{1}%
    \authorsep%
    {#2}%
    \unskip%
    \ifx\temp@one\@empty%
      \@ifundefined{Xname@0}%
        {\expandafter\xdef\csname Xname@0\endcsname{\filtername#2\@nil}}%
        {\expandafter\protected@xdef\csname Xname@0\endcsname%
            {\csname Xname@0\endcsname,\ \filtername#2\@nil}}%
    \else%
      \textsuperscript{\smash{{%
        \@for\@@affmark:=#1\do{\edef\affnum{\@ifundefined{X@\@@affmark}{1}{\jmkRef{\@@affmark}}}%
        \@ifundefined{Xname@\@@affmark}%
          {\expandafter\xdef\csname Xname@\@@affmark\endcsname{\filtername#2\@nil}}%
          {\expandafter\protected@xdef\csname Xname@\@@affmark\endcsname%
            {\csname Xname@\@@affmark\endcsname,\ \filtername#2\@nil}}%
        \unskip\sep\ifnum\addcount=1\else\affnum\fi\let\sep=,}%
      }}%
      \unhbox\authmarkbox}%
    \fi%
    \def\temp@three{#3}%
    \def\temp@four{#4}%
    \ifx\temp@three\@empty%
    \else%
      \hskip1\@p@t%3
      \orchid{\temp@three}%
    \fi%
    \ifx\temp@four\@empty%
    \else%
      \ifx\temp@three\@empty%
        \hskip3\@p@t%
      \else%
        \ %
      \fi%
      \temp@four%
    \fi%
    \gdef\authorsep{\ \ $|$ \ }%%%
    \global\let\sep\@empty\global\let\@corref\@empty%
  }%
}%

%%\if@purexml\else%
%
\newcounter{affn}%
\renewcommand\theaffn{\arabic{affn}}%
%
\def\jmkaddress{}%
\def\miscjmkaddress{}%
\def\addsep{\par\vskip0\@p@t}%
%
\newcount\addcount% To check the count of address
%
\def\address{\advance\addcount\@ne\@ifstar\@@corraddress\@@address}%
%
\newcommand{\@@corraddress}[2][]{\gdef\@corresinfo{#2}}%
%


\newcommand{\@@address}[2][]{}

\newcounter{lastauthor} % Definiše se novi brojač

\renewcommand{\@@address}[2][]{\def\temp{#1}%
\g@addto@macro\jmkaddress{\hsize=\textwidth%
\def\baselinestretch{1}%
\stepcounter{affn}%
\xdef\@currentlabel{\theaffn}%
\jmkLabel{#1}%
\addressfont\ifx#1\empty\else\@hangfrom{\textsuperscript{\smash{\theaffn}}}%
\fi%
#2\hphantom{1}\setcounter{lastauthor}{4}%%
\ifnum\theaffn=\value{lastauthor}\else\hskip-3\@p@t\authorsep\fi%
%\par%\vskip3\@p@t
}}%



%% email details %%

\newcount\emailcount%

%\def\jmkemail{}%
%\def\emailand{\ and\ }%
%\def\@emailsep{\unskip\advance\emailcount by -1\emailand}%

%\newcommand{\email}[1]{%%\global\advance\emailcount\@ne%
%\let\emailcount\corrauthcount%
%\g@addto@macro\jmkemail{%
%\def\emailau{\jmkRef{corrauth\thecorrauthcount};}%
%\emailsep{\emailau\ #1}\def\emailsep{\@emailsep}}}%

\newcommand{\email}[1]{\href{mailto:#1}{#1}}%

%
%% Correspondence details
%

%\def\corresinfotext{Correspondence should be addressed to }%
%\newcommand{\corresinfo}{{\hsize=\textwidth\corresfont\corresinfotext\jmkemail\par}}%

\gdef\@corresinfo{}%
\def\corres#1{\gdef\@corresinfo{#1}}%


\gdef\@editorinfo{}%
\def\editor#1{\gdef\@editorinfo{#1}}
%beginv5
%\newcommand{\corresinfo}{{\corresfont\baselineskip11\@p@t\textbf{Correspondence}\par \@corresinfo\par}}%
%endv5

%beginv6
%\newcommand{\corresinfo}{}

%endv6

\gdef\@presentaddress{}%
%beginv5
\def\presentaddress#1{\gdef\@presentaddress{#1}}%
%endv5

%beginv6
%\def\presentaddress#1{\gdef\@presentaddress{\hsize=\textwidth\corresfont\baselineskip11\@p@t\textbf{Present address}\par #1}}%
%endv6



%
\def\received#1{\gdef\@received{#1}}\gdef\@received{Added at production}%
\def\revised#1{\gdef\@revised{#1}}\gdef\@revised{Added at production}%
\def\accepted#1{\gdef\@accepted{#1}}\gdef\@accepted{Added at production}%
\def\published#1{\gdef\@published{Published #1}}\gdef\@published{Added at production}%
   \def\xmlreceived{\gdef\@received{\ifnum\artActypcnt=1\relax Received:\ref{MSSreceiveddate}\ReceivedMONTH\nobreakspace{}\kern.5\@p@t\ReceivedYEAR,\else Received:\ref{MSSreceiveddate}\nobreakspace{}\kern.5\@p@t\ReceivedDATE\nobreakspace{}\kern.5\@p@t\ReceivedMONTH\nobreakspace{}\kern.5\@p@t\ReceivedYEAR\fi}}\gdef\@received{}%
   \def\xmlrevised{\gdef\@revised{Revised:\ref{MSSrevised}\nobreakspace{}\kern.5\@p@t\RevisedDATE\nobreakspace{}\kern.5\@p@t\RevisedMONTH\nobreakspace{}\kern.5\@p@t\RevisedYEAR}}\gdef\@revised{}%
   \def\xmlaccepted{\gdef\@accepted{\ifnum\artActypcnt=1\relax in final form\ref{ACCdate}\AccMONTH\nobreakspace{}\kern.5\@p@t\AccYEAR\else Accepted:\ref{ACCdate}\nobreakspace{}\kern.5\@p@t\AccDATE\nobreakspace{}\kern.5\@p@t\AccMONTH\nobreakspace{}\kern.5\@p@t\AccYEAR\fi}}\gdef\@accepted{}%
   \def\xmlacceptedonline{\gdef\@acceptedonline{Accepted article online\ref{PUBaccptdate}\nobreakspace{}\kern.5\@p@t\AccDATEi\nobreakspace{}\kern.5\@p@t\AccMONTHi\nobreakspace{}\kern.5\@p@t\AccYEARi}}\gdef\@acceptedonline{}%
   \def\xmlpubdate{\gdef\@pubdate{Published on:\ref{PUBEVdate}\nobreakspace{}\kern.5\@p@t\PubDATE\nobreakspace{}\kern.5\@p@t\PubMONTH\nobreakspace{}\kern.5\@p@t\PubYEAR}}\gdef\@pubdate{}%

\newif\ifarticletype\global\articletypefalse

\def\pagenumberalign#1{\hbox to 22.5\@p@t{#1}}%
\def\rhvertbar{\hspace*{7\@p@t}\smash{\raisebox{-2.8\@p@t}{\rule{.5\@p@t}{10\@p@t}}}\hspace*{7\@p@t}}%
\def\foot@pagenumber@align#1{\hbox to 15.75\@p@t{#1}}%
\def\foot@vertbar{\hspace*{7\@p@t}\smash{\raisebox{-1\@p@t}{\rule{.5\@p@t}{9.25\@p@t}}}\hspace*{7\@p@t}}%
\def\authorseprule#1{\gdef\author@seprule{\hspace*{7\@p@t}#1\hspace*{7\@p@t}}}\authorseprule{\smash{\raisebox{.1\@p@t}{\hbox{\rule{.5\@p@t}{8.5\@p@t}}}}}


%
%
\def\rhlogo{} %\includegraphics{rhlogo}}

\def\thepagedef{{\pagenumfont\thepage\ of \@LastPg}}%

\def\headfootrule#1{\gdef\@headfootrule{#1}}\headfootrule{{\rule{\textwidth}{.5\@p@t}}}%

%beginv5
\def\historykern#1{\gdef\history@kern{#1}}\historykern{\nobreakspace{}\kern.5\@p@t}
\def\@dummy@received{\textcolor{black}{\textbf{Received:}\history@kern{}\history@kern{}\@received}}%
\def\@dummy@revised{\textcolor{black}{\textbf{Revised:}\history@kern{}\history@kern{}\@revised}}%
\def\@dummy@accepted{\textcolor{black}{\textbf{Accepted:}\history@kern{}\history@kern{}\@accepted}}%
\def\@dummy@pubdate{\textcolor{red}{Published\history@kern{}on:\history@kern{}\history@kern{}Added at production}}%
%endv5

%beginv6
%\def\historykern#1{\gdef\history@kern{#1}}\historykern{\nobreakspace{}\kern.5\@p@t}
%\def\@dummy@received{ }%
%\def\@dummy@revised{ }%
%\def\@dummy@accepted{ }%
%\def\@dummy@pubdate{ }%
%endv6

\def\historydates#1{\gdef\@history@dates{#1}}%
\historydates{{\FIfont%\titlepageheadfont%
  \ifx\@received\@empty\@dummy@received\else\@dummy@received\authorsep\fi%
  \ifx\@revised\@empty\@dummy@revised\else\@dummy@revised\authorsep\fi%
  \ifx\@accepted\@empty\@dummy@accepted\else\@dummy@accepted\fi%
%  \ifx\@pubdate\@empty\@dummy@pubdate\else\@pubdate\fi%
}}%
%

%v6
\def\doiheadtext#1{\gdef\@DOI@text{#1}}\doiheadtext{%DOI: dddxxx/xxxx\nobreakspace{}
}%



\def\head@titlepage@def#1#2{\vbox{%
  \hbox to \textwidth{#1\dummyalphabet}\par\vspace*{2.5\@p@t}%
  %beginv5
  %\@headfootrule\par
  %endv5
  \vspace*{9\@p@t}%
  \smash{{\titlepageheadfont\@DOI@text#2}}\par\vspace*{9\@p@t}%
  \vspace*{20.5\@p@t}% -2.5
  \smash{\hbox to \textwidth{\rhlogo}}\par%
}}%
%
\newskip\foot@titlepage@raise@skip\global\foot@titlepage@raise@skip23\@p@t%

\def\oddhead@titlepage@info{\head@titlepage@def{\hfill}{\@doi}}%\@history@dates
\def\evenhead@titlepage@info{\oddhead@titlepage@info}%

\def\oddfoot@titlepage@info{%123
  \vbox{%
    \hsize\textwidth%
    {%
      {\raggedright\fontsize{6.25}{8}\selectfont\ifx\@copyright\empty\vphantom{AAA}\else%
      \noindent\vskip15\@p@t%%-20
      %\rule{\textwidth}{.5\@p@t}%
	 \hdashrule{\textwidth}{0.5pt}{1.5pt}
      \vskip1\@p@t%
      \@copyright\par\fi}%AAA}\@copyright
      \vskip-23\@p@t%
      \noindent%
      \rule{\textwidth}{.5\@p@t}%
      \vskip-20\@p@t%
      {%
        \fontsize{7}{8}\selectfont%
         {\it \@journal}, \@copyyear;v\@volume:\@FirstPg--\@LastPg%
		 \\[-20pt]
		 \href{https://doi.org/\thearticledoi}{https://doi.org/\thearticledoi}
        \hfill%
        %wileyonlinelibrary.com/journal/%
        %\hfill%
        %\copyright\ \@copyyear\ Copyright Holder Name%
        %\quad%
        %\foot@vertbar%
        %\quad
        {\pagenumfont\thepage\ of \@LastPg}%
      }%
      \vskip10\@p@t%
    }%
  }%
}%
\def\evenfoot@titlepage@info{\oddfoot@titlepage@info}%
%\def\thepagedef{{\pagenumfont\thepage\ of \@LastPg}}%
%
\def\ps@titlepage{%
  \def\@oddfoot{\oddfoot@titlepage@info}%
  \def\@evenfoot{\evenfoot@titlepage@info}%
  \def\@oddhead{\oddhead@titlepage@info}%
  \def\@evenhead{\evenhead@titlepage@info}%
  \let\@mkboth\markboth%
}%


%% Abstract %%

%% Article Abstract
%% Author name in Running Head

\newcount\lastauthorcnt



\newif\if@suppimage \global\@suppimagefalse
\newif\ifAbstractexist%
\global\Abstractexistfalse%
%% Article Abstract
\renewcommand\abstract[2][]{%
  \global\Abstractexisttrue%
  \gdef\@abstract{%
    \begin{tcolorbox}[
      colback=AbstractBG,
      left=3.5mm,
      right=3.5mm,
      top=3.5mm,
      bottom=3.5mm,
      boxsep=0\@p@t,
      boxrule=0\@p@t,
      arc=0\@p@t,
    ]
      \reset@font\abstractfont%
      {\absheadfont#1\par}%
      #2\par
    \end{tcolorbox}%
    \addcontentsline{toc}{section}{\abstractname}%
  }
}
\newcommand\transabstract[2][]{%
}


%% Correspondence
\def\correspondencename{Correspondence}
\def\correspondence#1{%
  \gdef\@correspondence{%
    \par\vspace*{3\@p@t}%
    {\corresfont\correspondencename\par}%
    {\corresfont#1\par}%
  }%
}\correspondence{}%

%% Funding Information
\let\@fundinginfo\empty
\def\fundingInfo#1{%
  \gdef\@fundinginfo{{%
    \reset@font\FIfont\textbf{Funding:} #1\par%
  }}%
}%

%% JEL Classification
\newcount\JELinfocount\JELinfocount0%
\newcount\commaJELinfocount\commaJELinfocount0%
\newcount\JELinfotempcount\JELinfotempcount0%
\newcount\tempJELinfocount\tempJELinfocount0%
%
\def\JELinfo#1{%
  \expandafter\protected@xdef\csname JELinfo\the\JELinfocount\endcsname{#1}%
  \global\advance\JELinfocount1%
}%
%
\def\JELinfohead#1{\gdef\@JELinfo@head{#1}}\JELinfohead{JEL Classification:\nobreakspace{}}%
\gdef\@JELinfo{{%
  \ifnum\JELinfocount<1\else%
  \reset@font\JELfont%
  \par\vspace*{3\@p@t}%
  \@JELinfo@head%
  \global\tempJELinfocount\JELinfocount%
  \global\advance\tempJELinfocount-1%
  \global\commaJELinfocount\tempJELinfocount%
  \global\advance\commaJELinfocount-1%
  \ifnum\JELinfocount=1%
    \ignorespaces\csname JELinfo0\endcsname%
  \else%
    \@whilenum\JELinfotempcount<\tempJELinfocount\do{%
      \ignorespaces\csname JELinfo\the\JELinfotempcount\endcsname\ %
      \global\advance\JELinfotempcount1%
    }%
    \csname JELinfo\the\JELinfotempcount\endcsname\par\vspace*{-\baselineskip}%
  \fi%
  \fi%
\vfill}}%

\definecolor{zeropercent}{cmyk}{0,0,0,0}
\definecolor{tenpercent}{cmyk}{0,0,0,0.10}
\definecolor{thirtyfivepercent}{cmyk}{0,0,0,0.35}
\definecolor{sixtyfivepercent}{cmyk}{0,0,0,0.65}
\definecolor{seventyfivepercent}{cmyk}{0,0,0,0.75}

%
\newbox\abs@boxa%
\newbox\abs@boxb%
\newbox\abs@boxc%
%
\newdimen\abs@dima%
\newdimen\abs@dimb%
\newdimen\abs@dimc%
%
\def\absCOvrule{\vrule width0.5\@p@t}%
\def\authCOvrule{{\color{zeropercent}\vrule width\abs@colii@hsize}}%

\def\FWauthCOvrule{{\color{zeropercent}\vrule width\textwidth}}%

\newif\ifFWabstract%
%
\gdef\printabstractpart{\vskip-18\@p@t%%
  \noindent{\reset@font\addressfont\jmkaddress\par}%
  \removelastskip\vskip10\@p@t%
  \ifx\@corresinfo\empty\else{\reset@font\FIfont\textbf{Correspondence:~}\@corresinfo\par}\fi%
  \removelastskip\vskip4.25\@p@t%\@history@dates
  \ifx\@history@dates\empty\else{\@history@dates\par}\fi%
  \removelastskip\vskip4.25\@p@t%
  \ifx\@editorinfo\empty\else{\reset@font\FIfont\@editorinfo\par}\fi%
  %\ifx\@presentaddress\empty\else{\@presentaddress\par}\fi%
  %\ifx\@keywords\@empty\else\reset@font\FIfont\@keywords\fi%
 \removelastskip\vskip4.25\@p@t%
  \ifx\@fundinginfo\empty\else{\@fundinginfo}\fi%
  \removelastskip\vskip4.25\@p@t%
  %\ifx\@presentaddress\empty\else{\@presentaddress\par}\fi%
  \ifx\@keywords\@empty\else\reset@font\FIfont\textbf{Keywords: }\@keywords\fi%
  %\removelastskip\vskip8\@p@t%
  %\ifx\@JELinfo\empty\else{\vskip2\@p@t\@JELinfo\par}\fi%
  \ifx\@abstract\@empty\else\@abstract\fi%
  %\removelastskip\vskip15\@p@t%
  %\ifx\@keywords\@empty\else\@keywords\fi%
  %\ifx\@msccode\@empty\else\vskip15\@p@t\@msccode\fi%
}

%% Printing Abstract
%% Printing Abstract
\long\def\printabstract{%%
  \bgroup%
    \setlength{\parindent}{\z@}%
    \printabstractpart%
  \egroup%
}%

%% Legal Statement
\def\legalstatement#1{%
  \gdef\@temp{#1}%
  \ifx\@temp\@empty%
    \gdef\@legalstatement{}%
  \else%
    \gdef\@legalstatement{%
      \smash{{\color{thirtyfivepercent}\@headfootrule}}\par\vspace*{-.45\baselineskip}%
      \parbox[t]{\textwidth}{{\color{seventyfivepercent}\legalstatementfont#1\dummyalphabet\par}}\par\vspace*{4.75\@p@t}%
    }%
  \fi%
}\legalstatement{}%


%% Keywords %%

\def\keywords#1{\def\@keywords{#1\par}}\def\@keywords{}%

\def\MSC{\@ifnextchar[{\@@MSC}{\@MSC}}\def\@msccode{}%
\def\@@MSC[#1]#2{%
  \def\@msccode{{\keywordsheadfont\textbf{#1}\par\removelastskip\nointerlineskip\vskip6\@p@t\keywordsfont#2\par}}%
}
\def\@MSC#1{%
  \def\@msccode{{\keywordsheadfont\textbf{MSC (2020)}\par\removelastskip\nointerlineskip\vskip6\@p@t\keywordsfont#1\par}}%
}%

\def\@fnsymbol#1{\ensuremath{\ifcase#1\or \dagger\or \ddagger\or
   \S\or \P\or \#\or \|\or **\or \dagger\dagger
   \or \ddagger\ddagger\or \S\S\or \P\P\or \#\#\or \|\|\else\@ctrerr\fi\relax}}

\renewcommand{\maketitle}{\par%
  \@afterindentfalse%
   \begingroup
    \renewcommand{\thefootnote}{\fnsymbol{footnote}}%
    \def\@makefnmark{\hbox to\z@{$\m@th^{\@thefnmark}$\hss}}%
    \long\def\@makefntext##1{\noindent
            \hbox to0.1em{\hss$\m@th^{\@thefnmark}$}##1}%
%
    \if@twocolumn
      %\ifnum \col@number=\@ne
         \twocolumn[\@maketitle]%
      %\else
         %\twocolumn[\@maketitle]%
      %\fi
    \else
    \@maketitle%
    \fi
    \thispagestyle{titlepage}\@thanks%
    %\insert\footins{authfndefns}\par%12345
  \endgroup
%  \let\footnotetext\oldfootnotetext%
  \setcounter{footnote}{0}%
  \let\thanks\relax
%  \let\footnote\thanks%
  \let\maketitle\relax\let\@maketitle\relax
  \gdef\@thanks{}\gdef\@author{}\gdef\@title{}
  \@afterheading%
}

\newskip\adjtitleskip%

\adjtitleskip=10\@p@t plus4\@p@t minus2\@p@t
\RequirePackage{tikzpagenodes}
\renewcommand{\@maketitle}{%
{%
\begin{tikzpicture}[remember picture,overlay]%
%%
    %\node[inner sep=0mm]    (yy) at ([yshift=0mm]current page.center){\includegraphics[scale=1]{1p}};
%%
    \node[anchor=north east,inner sep=0mm]
    (LOG) at ([yshift=0mm]current page header area.north east){\includegraphics[width=26.5mm]{Wiley_logo.eps}};
    \node[anchor=north east,inner sep=0mm]
    (LOG1) at ([yshift=-11.75mm]current page header area.north east){\includegraphics[width=56mm]{allergy.eps}};
%
\end{tikzpicture}
  \hsize=\textwidth\parindent=0\@p@t%
  \thispagestyle{titlepage}% empty
  \let\footnote\thanks%
  \vskip-1.5mm
  \smash{\@journalfont\@journal}%
  \vskip24.36mm%
  \@@articletype%
  \vskip8.6mm%
  \ifx\@title\empty\else{\@title\par}\fi%
  %\ifx\@subtitle\empty\else{\subtitlefont\@subtitle\par}\fi%
  \removelastskip\vskip8\@p@t%
    {\artauthors\par}%
    \removelastskip\vskip8.25\@p@t%
     %{\jmkaddress\par}%
    %\removelastskip\vskip9\@p@t%
    %\ifx\@corresinfo\empty\else{\@corresinfo\par}\fi%
    %\removelastskip\vskip8\@p@t%
    %\ifx\@presentaddress\empty\else\@presentaddress\fi%
    %\removelastskip\vskip8\@p@t%
    %{\history\par}%
    %\removelastskip\vskip12\@p@t%
  %   \ifx\@abstract\empty\else{\@abstract\par}\fi%
    \removelastskip\vskip19\@p@t%
  %   \ifx\@keywords\empty\else{\@keywords\par}\fi%
    \printabstract
  %%   \vspace*{10\@p@t plus4\@p@t minus2\@p@t}%
    \removelastskip\vskip\adjtitleskip%
  }%
}%

%\renewcommand\thefootnote{\@arabic\c@footnote}%

%
%%

%%%%%%%%%%%%%%%%%%%%%%%section defn%%%%%%%%%%%%%%%%%%%%%%

%\setcounter{secnumdepth}{3}
\newcounter {chapter}[part]% for bookmarks
\renewcommand\thepart         {\@Roman\c@part}
\renewcommand\thesection      {\@arabic\c@section}
\renewcommand\thesubsection   {\thesection.\@arabic\c@subsection}
\renewcommand\thesubsubsection{\thesubsection.\@arabic\c@subsubsection}
\renewcommand\theparagraph    {\thesubsubsection.\@arabic\c@paragraph}
\renewcommand\thesubparagraph {\theparagraph.\@arabic\c@subparagraph}
%%
\renewcommand\part{\par
   \addvspace{4ex}%
   \@afterindentfalse
   \secdef\@part\@spart}
%%
\def\@part[#1]#2{%
    \ifnum \c@secnumdepth >\m@ne
      \refstepcounter{part}%
      \addcontentsline{toc}{part}{\thepart\hspace{1em}#1}%
    \else
      \addcontentsline{toc}{part}{#1}%
    \fi
    {\parindent \z@ \raggedright
     \interlinepenalty \@M
     \normalfont
     \ifnum \c@secnumdepth >\m@ne
       \Large\bfseries \partname~\thepart
       \par\nobreak
     \fi
     \huge \bfseries #2%
     \markboth{}{}\par}%
    \nobreak
    \vskip 3ex
    \@afterheading}

\def\@spart#1{%
    {\parindent \z@ \raggedright
     \interlinepenalty \@M
     \normalfont
     \huge \bfseries #1\par}%
     \nobreak
     \vskip 3ex
     \@afterheading}

\if@twocolumn
\newcommand\bmsection{\@startsection{section}{1}{\z@}{-12\@p@t \@plus -2\@p@t \@minus -2\@p@t}{6\@p@t}{\fontsize{10}{12}\selectfont\bfseries}}%
\newcommand\bmsubsection{\@startsection{subsection}{2}{\z@}{-12\@p@t \@plus -2\@p@t \@minus -2\@p@t}{6\@p@t}{\fontsize{10}{12}\selectfont\bfseries}}%
\newcommand\bmsubsubsection{\@startsection{subsubsection}{3}{\z@}{-12\@p@t \@plus -2\@p@t \@minus -2\@p@t}{6\@p@t}{\fontsize{10}{12}\selectfont\bfseries}}%

\renewcommand\section{\@startsection{section}{1}{\z@}{-27\@p@t \@plus -2\@p@t \@minus -2\@p@t}{8\@p@t}{\fontsize{10}{12}\selectfont\bfseries}}%
\renewcommand\subsection{\@startsection{subsection}{2}{\z@}{-23\@p@t \@plus -2\@p@t \@minus -2\@p@t}{8\@p@t}{\fontsize{10}{12}\selectfont\bfseries}}%
\renewcommand\subsubsection{\@startsection{subsubsection}{3}{\z@}{-20\@p@t \@plus -2\@p@t \@minus -2\@p@t}{8\@p@t}{\fontsize{9}{11}\selectfont\bfseries}}%
\renewcommand\paragraph{\@startsection{paragraph}{4}{\z@}{-12\@p@t \@plus -2\@p@t \@minus -2\@p@t}{0\@p@t}{\fontsize{9}{11}\selectfont\bfseries}}
\renewcommand\subparagraph{\@startsection{subparagraph}{5}{\z@}{-10\@p@t \@plus -2\@p@t \@minus -2\@p@t}{-6\@p@t}{\fontsize{9}{11}\selectfont\bfseries}}%%
%
\else
\newcommand\bmsection{\@startsection{section}{1}{\z@}{-12\@p@t \@plus -2\@p@t \@minus -2\@p@t}{6\@p@t}{\fontsize{11}{13}\selectfont\bfseries}}%
\newcommand\bmsubsection{\@startsection{subsection}{2}{\z@}{-12\@p@t \@plus -2\@p@t \@minus -2\@p@t}{6\@p@t}{\fontsize{11}{13}\selectfont\bfseries}}%
\newcommand\bmsubsubsection{\@startsection{subsubsection}{3}{\z@}{-12\@p@t \@plus -2\@p@t \@minus -2\@p@t}{6\@p@t}{\fontsize{11}{13}\selectfont\bfseries}}%

\renewcommand\section{\@startsection{section}{1}{\z@}{-27\@p@t \@plus -2\@p@t \@minus -2\@p@t}{8\@p@t}{\fontsize{11}{13}\selectfont\bfseries}}%
\renewcommand\subsection{\@startsection{subsection}{2}{\z@}{-23\@p@t \@plus -2\@p@t \@minus -2\@p@t}{8\@p@t}{\fontsize{11}{13}\selectfont\bfseries}}%
\renewcommand\subsubsection{\@startsection{subsubsection}{3}{\z@}{-20\@p@t \@plus -2\@p@t \@minus -2\@p@t}{8\@p@t}{\fontsize{10}{12}\selectfont\bfseries}}%
\renewcommand\paragraph{\@startsection{paragraph}{4}{\z@}{-12\@p@t \@plus -2\@p@t \@minus -2\@p@t}{0\@p@t}{\fontsize{10}{12}\selectfont\bfseries}}
\renewcommand\subparagraph{\@startsection{subparagraph}{5}{\z@}{-10\@p@t \@plus -2\@p@t \@minus -2\@p@t}{-6\@p@t}{\fontsize{10}{12}\selectfont\bfseries}}%%
\fi
%
%
\newskip\secruleskip\secruleskip12\@p@t%
%\def\secrule{\color{sixtyfivepercent}\hspace*{\secruleskip}\raisebox{0\@p@t}{\rule{.5\@p@t}{7.5\@p@t}}\hspace*{\secruleskip}}%
%\def\subsecrule{\color{sixtyfivepercent}\secrule}%
%\def\subsubsecrule{\color{sixtyfivepercent}\hspace*{\secruleskip}\raisebox{0\@p@t}{\rule{.5\@p@t}{6.5\@p@t}}\hspace*{\secruleskip}}%

\def\secrule{\hspace*{5pt}\raisebox{-1.8\@p@t}{\rule{0.5\@p@t}{8.5\@p@t}}\hspace*{5pt}}%
\def\subsecrule{\secrule}%
\def\subsubsecrule{\hspace*{5pt}\raisebox{-1.8\@p@t}{\rule{0.5\@p@t}{8.5\@p@t}}\hspace*{5pt}}%
\def\paragraphsecrule{\hspace*{5pt}\raisebox{-1.8\@p@t}{\rule{0.5\@p@t}{8.5\@p@t}}\hspace*{5pt}}%
\def\subparagraphsecrule{\hspace*{5pt}\raisebox{-1.8\@p@t}{\rule{0.5\@p@t}{8.5\@p@t}}\hspace*{5pt}}%


\def\@sectioncntformat#1{{\ifappendixsec\csname the#1\endcsname\ \secrule\enskip\else\csname the#1\endcsname\ \secrule\enskip\fi}}%
\def\@subsectioncntformat#1{{\ifappendixsec\csname the#1\endcsname\  \subsecrule\enskip\else\csname the#1\endcsname\  \subsecrule\enskip\fi}}%
\def\@subsubsectioncntformat#1{{\ifappendixsec\csname the#1\endcsname\  \subsubsecrule\enskip\else\csname the#1\endcsname\  \subsubsecrule\enskip\fi}}%
\def\@paragraphcntformat#1{{\ifappendixsec\csname the#1\endcsname\  \paragraphsecrule\enskip\else\csname the#1\endcsname\  \paragraphsecrule\enskip\fi}}%
\def\@subparagraphcntformat#1{{\ifappendixsec\csname the#1\endcsname\   \subparagraphsecrule\enskip\else\csname the#1\endcsname\   \subparagraphsecrule\enskip\fi}}%


%\def\@paragraphcntformat#1{{\ifappendixsec\csname the#1\endcsname\ \else\csname the#1\endcsname.\ \fi}}%
%\def\@subparagraphcntformat#1{#1}
%
%%\renewcommand\paragraph{\@startsection{paragraph}{4}{\z@}{-10\@p@t \@plus -2\@p@t \@minus %%-2\@p@t}{0.001\@p@t}{\fontsize{11}{13}\selectfont\itshape}}%
%%\renewcommand\subparagraph{\@startsection{subparagraph}{5}{\z@}{-10\@p@t \@plus -2\@p@t \@minus %%-2\@p@t}{-12\@p@t}{\fontsize{10}{12}\selectfont\itshape}}%

\def\@sect#1#2#3#4#5#6[#7]#8{%
   \ifnum #2>\c@secnumdepth%
      \edef\@svsec{\noindent}%
   \else%
      \refstepcounter{#1}%
      \let\@@protect\protect%
      \def\protect{\noexpand\protect\noexpand}%
      \edef\@svsec{\csname @#1cntformat\endcsname{#1}}%
      \let\protect\@@protect%
   \fi%
   \@tempskipa #5\relax%

   \ifdim \@tempskipa>\z@%
      \ifnum#2=1%
         \begingroup #6\relax%\@hangfrom%
            {\noindent\hskip #3\relax{\@svsec}}%
            {\interlinepenalty\@M{{#8}}\par}%
         \endgroup%
      \else%
        \ifnum#2=2%
          \begingroup #6\relax%\@hangfrom%
              {\noindent\hskip #3\relax\@svsec}%
              {\interlinepenalty\@M{#8}\par}%
          \endgroup%
        \else%
          \ifnum#2=3%
            \begingroup #6\relax%\@hangfrom%
              {\noindent\hskip #3\relax\@svsec}%
              {\interlinepenalty\@M{#8}\par}%
            \endgroup%
          \else%
            \ifnum#2=4%
              \begingroup #6\relax%\@hangfrom%
                {\noindent\hskip #3\relax\@svsec}%
                {\interlinepenalty\@M{\textcolor{black!75}{#8}}\par}%
              \endgroup%
            \else%
              \begingroup #6\relax%\@hangfrom
                {\noindent\hskip #3\relax\@svsec}%
                {\interlinepenalty\@M#8\par}%
              \endgroup%
            \fi%
          \fi%
        \fi%
      \fi%
      \csname #1mark\endcsname{#7}%
      \addcontentsline{toc}{#1}{%
        \ifnum #2>\c@secnumdepth \else%
          \protect\numberline{\csname the#1\endcsname}%
        \fi%
        #7}%
    \else%
      \def\@svsechd{%
         #6{\hskip #3\relax%
         \@svsec \textcolor{black!75}{#8}\hspace*{4\@p@t}}%
      }%
   \fi%
   \@xsect{#5}%
}%

%v6 paragraph
%\renewcommand\paragraph{\@startsection{paragraph}{4}{\z@}{-10\@p@t \@plus -2\@p@t \@minus -2\@p@t}{-12\@p@t}{\subparagraphfont}}%
%\renewcommand\subparagraph{\@startsection{subparagraph}{5}{\z@}{-10\@p@t \@plus -2\@p@t \@minus -2\@p@t}{-12\@p@t}{\subparagraphfont}}%
\def\@sect#1#2#3#4#5#6[#7]#8{%
   \ifnum #2>\c@secnumdepth%
      \edef\@svsec{\noindent}%
   \else%
      \refstepcounter{#1}%
      \let\@@protect\protect%
      \def\protect{\noexpand\protect\noexpand}%
      \edef\@svsec{\csname @#1cntformat\endcsname{#1}}%
      \let\protect\@@protect%
   \fi%
   \@tempskipa #5\relax%
   \ifdim \@tempskipa>\z@%
      \ifnum#2=1%
         \begingroup #6\relax%\@hangfrom%
            {\noindent\hskip #3\relax{\@svsec}}%
            {\interlinepenalty\@M{{#8}}\par}%
         \endgroup%
      \else%
         \ifnum#2=2%
            \begingroup #6\relax%\@hangfrom%
               {\noindent\hskip #3\relax\@svsec}%
               {\interlinepenalty\@M{#8}\par}%
            \endgroup%
         \else%
            \begingroup #6\relax%\@hangfrom
               {\noindent\hskip #3\relax\@svsec}%
               {\interlinepenalty\@M{#8}\par}%
            \endgroup%
         \fi%
      \fi%
      \csname #1mark\endcsname{#7}%
      \addcontentsline{toc}{#1}{%
        \ifnum #2>\c@secnumdepth \else%
          \protect\numberline{\csname the#1\endcsname}%
        \fi%
        #7}%
   \else%
      \def\@svsechd{%
         #6{\hskip #3\relax%
         \@svsec #8.\hspace*{6\@p@t}}%
      }%
   \fi%
   \@xsect{#5}%
}%


\def\@ssect#1#2#3#4#5#6{%
   %#1#2#3#4#5#6
   \@tempskipa #4\relax%
   \ifdim\@tempskipa>\z@%
      \ifnum#1=1%
         \begingroup%
            #5{\@hangfrom{\hskip #2}%
            \interlinepenalty \@M\uppercase{#6}\@@par}%
         \endgroup%
         \addcontentsline{toc}{section}{#6}%
      \else%
         \begingroup%
            #5{\@hangfrom{\hskip #2}%
            \interlinepenalty \@M#6\@@par}%
         \endgroup%
      \fi%
   \else%
      \def\@svsechd{#5{\hskip #2\relax #6}}%
   \fi%
   \@xsect{#4}%
}%
%
\def\@startsection#1#2#3#4#5#6{%
   \if@noskipsec\leavevmode\fi%
   \par%
   \@tempskipa #4\relax%
   \@afterindenttrue%
   \ifdim \@tempskipa <\z@%
      \@tempskipa-\@tempskipa%
      \@afterindentfalse%
   \fi
   \if@nobreak
      %\ifnum#2=2%
      %   \vspace*{-2.4\@p@t}%
      %\fi%
      \everypar{}%
   \else%
      \addpenalty\@secpenalty%
      \addvspace\@tempskipa%
   \fi%
   \@ifstar%
      {\@ssect{#2}{#3}{#4}{#5}{#6}}%
      {\@dblarg{\@sect{#1}{#2}{#3}{#4}{#5}{#6}}}}%

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%% Regular pagestyle %%
%

\def\@authormark{}%
\def\authormark#1{%
\markboth{{{#1}}}{{{#1}}}\gdef\@authormark{\def\etal{\textit{ET AL.}}\let\AQ\AHAQ#1}\gdef\@authormarkforcite{\undefined\def\etal{\textit{et al.}}#1}}%
\def\etal{\textit{et al.}}%

\newskip\rhverticalsep\rhverticalsep11.5\@p@t%
\def\oddpage@headings@def{\hspace*{\rhverticalsep}\pagenumberalign{\hss\thepagedef}}%
\def\evenpage@headings@def{\pagenumberalign{\thepagedef\hss}\hspace*{\rhverticalsep}}%

\def\leftmarkdef{{\runningheadfont\@authormark}}%
\def\rightmarkdef{{\runningheadfont\@titlemark}}%
%

\def\head@headings@def#1{%
  \vbox{%
    \vskip3.5mm%
    \vrule width \textwidth height .5\@p@t depth 0\@p@t%
    \vskip0\@p@t%
    \hbox to \textwidth{#1\dummyalphabet}%
  }%
}%


\def\oddhead@headings@info{\head@headings@def{\hfill\oddpage@headings@def}}%
\def\evenhead@headings@info{\head@headings@def{\evenpage@headings@def\hfill{\pagenumfont{\itshape\@journal}, \@copyyear}}}%

\def\ps@headings{%
\let\@oddfoot\@empty\let\@evenfoot\@empty%
  \def\@oddfoot{\oddhead@headings@info}%
  \def\@evenfoot{\evenhead@headings@info}%
\let\@mkboth\markboth%
}%
\pagestyle{headings}%
%

%% Macros for Section headings %%

\setcounter{secnumdepth}{5}%
%

\newif\if@FTSection%
\global\@FTSectionfalse%

\newif\if@unnumsection%
\@unnumsectionfalse%


%% Macros for Bibliography %%

%\newif\ifbackmatter\newcommand{\backmatter}{\par\global\backmattertrue}%

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% Bibliography %%%%%%%%%%%%%%%%%%%%%%%%%%%
%
\if@twocolumn
\if@Vancouverrefstyle%
\IfFileExists{NJDnatbib.sty}{\usepackage[numbers,super,sort&compress]{NJDnatbib}}{\usepackage[numbers,super,sort&compress]{natbib}}%
   \bibliographystyle{WileyNJD-VANCOUVER}%
%   \bibpunct{}{}{, }{s}{}{}%
\renewcommand\@biblabel[1]{#1.}
\renewcommand\refname{\fontsize{9}{11.5}\selectfont  References}
 \renewcommand{\bibfont}{\reset@font\fontfamily{\rmdefault}\fontsize{8.5}{11}\selectfont\baselineskip=10\@p@t}%
\else%
\if@AMArefstyle%
   \IfFileExists{NJDnatbib.sty}{\usepackage[numbers,super]{NJDnatbib}}{\usepackage[numbers,super]{natbib}}%
   \bibliographystyle{WileyNJD-AMA}%
\renewcommand\@biblabel[1]{#1.}
 %%  \bibpunct{(}{)}{,}{n}{}{}%
\setcitestyle{super}
\renewcommand\refname{References}
  \renewcommand{\bibfont}{\reset@font\fontfamily{\rmdefault}\fontsize{8.5}{11}\selectfont\baselineskip=10\@p@t}%
\else%%
\if@Chemistryrefstyle%
\IfFileExists{NJDnatbib.sty}{\usepackage[numbers,super,sort&compress]{NJDnatbib}}{\usepackage[numbers,super,sort&compress]{natbib}}%
   \bibliographystyle{WileyNJD-WCMS}%
   \renewcommand\@biblabel[1]{#1.}
   \bibpunct{[}{]}{,}{n}{}{}%
\setcitestyle{open={[},close={]}}
\renewcommand\refname{References}
  \renewcommand{\bibfont}{\reset@font\fontfamily{\rmdefault}\fontsize{8.5}{11}\selectfont\baselineskip=10\@p@t}%
\else%
\if@AMSrefstyle%
   \usepackage[numbers]{NJDnatbib}%
      \bibliographystyle{wileyNJD-Chicago}%
   \renewcommand\@biblabel[1]{#1.\enskip}
   \bibpunct{(}{)}{,}{}{}{}%
\setcitestyle{numbers,square}
\renewcommand\refname{References}
  \renewcommand{\bibfont}{\reset@font\fontfamily{\rmdefault}\fontsize{8.5}{11}\selectfont\baselineskip=10\@p@t}%
\else%
\if@Harvardstyle%
   \usepackage[authoryear]{NJDnatbib}%
   \bibliographystyle{WileyNJD-Harvard}%
   \bibpunct{(}{)}{,}{}{}{}%
%\setcitestyle{numbers,square}
\renewcommand\refname{References}
  \renewcommand{\bibfont}{\reset@font\fontfamily{\rmdefault}\fontsize{8.5}{11}\selectfont\baselineskip=10\@p@t}%
\else%
\if@Chicagostyle%
   \usepackage[authoryear]{NJDnatbib}%
   \bibliographystyle{wileyNJD-Chicago}%
\renewcommand\refname{References}
 \renewcommand{\bibfont}{\reset@font\fontfamily{\rmdefault}\fontsize{8.5}{11}\selectfont\baselineskip=10\@p@t}%
\else%
\if@MPSstyle%
   \usepackage[numbers]{NJDnatbib}%
\bibliographystyle{WileyNJD-MPS}
   \renewcommand\@biblabel[1]{#1.}
%   \bibpunct{[}{]}{,}{n}{}{}%
\renewcommand\refname{References}
 \renewcommand{\bibfont}{\reset@font\fontfamily{\rmdefault}\fontsize{8.5}{11}\selectfont\baselineskip=10\@p@t}%
\else%
\if@APSstyle%
  \usepackage[numbers]{NJDnatbib}%
\bibliographystyle{WileyNJD-APS}
   \renewcommand\@biblabel[1]{#1.}
\renewcommand\refname{References}
 \renewcommand{\bibfont}{\reset@font\fontfamily{\rmdefault}\fontsize{8.5}{11}\selectfont\baselineskip=10\@p@t}%
\else%
\if@MLAstyle%
   \usepackage{NJDnatbib}%
\bibliographystyle{WileyNJD-MLA}
   \bibpunct{(}{)}{ and }{}{}{}%
\renewcommand\refname{References}
 \renewcommand{\bibfont}{\reset@font\fontfamily{\rmdefault}\fontsize{8.5}{11}\selectfont\baselineskip=10\@p@t}%
\else%
\if@APArefstyle
%   \usepackage[natbibapa]{NJDapacite}%
    \usepackage[apaciteclassic]{NJDapacite}
   \bibliographystyle{WileyNJD-APA}%
   \renewcommand\refname{References}
   \renewcommand{\APACrefbtitle}[2]{\textit{#2}}
 %  \bibpunct{(}{)}{; }{}{}{}%
   %%
 % \newcommand{\bibfont}{\reset@font\fontfamily{\rmdefault}\fontsize{8.5\@p@t}{11}\selectfont\baselineskip=10\@p@t}%
   %%
   \renewcommand{\APACrefnote}[1]{%
     \ifx\@empty#1\@empty
     \else
       {#1.}%
     \fi}%
   %%
   \renewcommand{\BTR}{}%%
   \renewcommand{\BNUM}{}% number (of a technical report)
   %%
   \renewcommand{\APACjournalVolNumPages}[4]{%
     \textit{#1}%             journal
     \ifx\@empty#2\@empty
     \else
       \unskip, \textit{#2}%  volume
     \fi
     \ifx\@empty#3\@empty
     \else
       \unskip({#3})%      issue number
     \fi
     \ifx\@empty#4\@empty
     \else
       \unskip, {#4}%      pages
     \fi}
   %%
\fi\fi\fi\fi\fi\fi%
\fi\fi\fi\fi
\else
%%
\if@Vancouverrefstyle%
\IfFileExists{NJDnatbib.sty}{\usepackage[numbers,super,sort&compress]{NJDnatbib}}{\usepackage[numbers,super,sort&compress]{natbib}}%
   \bibliographystyle{WileyNJD-VANCOUVER}%
%   \bibpunct{}{}{, }{s}{}{}%
\renewcommand\@biblabel[1]{#1.}
\renewcommand\refname{\fontsize{10}{11.5}\selectfont  References}
 \renewcommand{\bibfont}{\reset@font\fontfamily{\rmdefault}\fontsize{9.5}{13}\selectfont\baselineskip=11\@p@t}%
\else%
\if@AMArefstyle%
   \IfFileExists{NJDnatbib.sty}{\usepackage[numbers,super]{NJDnatbib}}{\usepackage[numbers,super]{natbib}}%
   \bibliographystyle{WileyNJD-AMA}%
\renewcommand\@biblabel[1]{#1.}
 %%  \bibpunct{(}{)}{,}{n}{}{}%
\setcitestyle{super}
\renewcommand\refname{References}
  \renewcommand{\bibfont}{\reset@font\fontfamily{\rmdefault}\fontsize{9.5}{13}\selectfont\baselineskip=11\@p@t}%
\else%%
\if@Chemistryrefstyle%
\IfFileExists{NJDnatbib.sty}{\usepackage[numbers,super,sort&compress]{NJDnatbib}}{\usepackage[numbers,super,sort&compress]{natbib}}%
   \bibliographystyle{WileyNJD-WCMS}%
   \renewcommand\@biblabel[1]{#1.}
   \bibpunct{[}{]}{,}{n}{}{}%
\setcitestyle{open={[},close={]}}
\renewcommand\refname{References}
  \renewcommand{\bibfont}{\reset@font\fontfamily{\rmdefault}\fontsize{9.5}{13}\selectfont\baselineskip=11\@p@t}%
\else%
\if@AMSrefstyle%TAj
   \usepackage[numbers]{NJDnatbib}%
      \bibliographystyle{wileyNJD-Chicago}%
   \renewcommand\@biblabel[1]{#1.\enskip}
   \bibpunct{(}{)}{,}{}{}{}%
\setcitestyle{numbers,square}
\renewcommand\refname{References}
  \renewcommand{\bibfont}{\reset@font\fontfamily{\rmdefault}\fontsize{9.5}{13}\selectfont\baselineskip=11\@p@t}%
\else%
\if@Harvardstyle%
   \usepackage[authoryear]{NJDnatbib}%
   \bibliographystyle{WileyNJD-Harvard}%
   \bibpunct{(}{)}{,}{}{}{}%
%\setcitestyle{numbers,square}
\renewcommand\refname{References}
  \renewcommand{\bibfont}{\reset@font\fontfamily{\rmdefault}\fontsize{9.5}{13}\selectfont\baselineskip=11\@p@t}%
\else%
\if@Chicagostyle%
   \usepackage[authoryear]{NJDnatbib}%
   \bibliographystyle{wileyNJD-Chicago}%
\renewcommand\refname{References}
 \renewcommand{\bibfont}{\reset@font\fontfamily{\rmdefault}\fontsize{9.5}{13}\selectfont\baselineskip=11\@p@t}%
\else%
\if@MPSstyle%
   \usepackage[numbers]{NJDnatbib}%
\bibliographystyle{WileyNJD-MPS}
   \renewcommand\@biblabel[1]{#1.}
%   \bibpunct{[}{]}{,}{n}{}{}%
\renewcommand\refname{References}
 \renewcommand{\bibfont}{\reset@font\fontfamily{\rmdefault}\fontsize{9.5}{13}\selectfont\baselineskip=11\@p@t}%
\else%
\if@APSstyle%
  \usepackage[numbers]{NJDnatbib}%
\bibliographystyle{WileyNJD-APS}
   \renewcommand\@biblabel[1]{#1.}
\renewcommand\refname{References}
 \renewcommand{\bibfont}{\reset@font\fontfamily{\rmdefault}\fontsize{9.5}{13}\selectfont\baselineskip=11\@p@t}%
\else%
\if@MLAstyle%
   \usepackage{NJDnatbib}%
\bibliographystyle{WileyNJD-MLA}
   \bibpunct{(}{)}{ and }{}{}{}%
\renewcommand\refname{References}
 \renewcommand{\bibfont}{\reset@font\fontfamily{\rmdefault}\fontsize{9.5}{13}\selectfont\baselineskip=11\@p@t}%
\else%
\if@APArefstyle
%   \usepackage[natb`ibapa]{NJDapacite}%
    \usepackage[apaciteclassic]{NJDapacite}
   \bibliographystyle{WileyNJD-APA}%
   \renewcommand\refname{References}
   \renewcommand{\APACrefbtitle}[2]{\textit{#2}}
 %  \bibpunct{(}{)}{; }{}{}{}%
   %%
 % \newcommand{\bibfont}{\reset@font\fontfamily{\rmdefault}\fontsize{8.5\@p@t}{11}\selectfont\baselineskip=10\@p@t}%
   %%
   \renewcommand{\APACrefnote}[1]{%
     \ifx\@empty#1\@empty
     \else
       {#1.}%
     \fi}%
   %%
   \renewcommand{\BTR}{}%%
   \renewcommand{\BNUM}{}% number (of a technical report)
   %%
   \renewcommand{\APACjournalVolNumPages}[4]{%
     \textit{#1}%             journal
     \ifx\@empty#2\@empty
     \else
       \unskip, \textit{#2}%  volume
     \fi
     \ifx\@empty#3\@empty
     \else
       \unskip({#3})%      issue number
     \fi
     \ifx\@empty#4\@empty
     \else
       \unskip, {#4}%      pages
     \fi}
   %%
\fi\fi\fi\fi\fi\fi%
\fi\fi\fi\fi
%%
\fi

%
%% Macros for Scheme %%
%
\def\ftype@scheme{1}%
\def\ext@scheme{los}%
\def\fps@scheme{tbp}%
\newcounter{scheme}%
\setcounter{scheme}{0}%
\renewcommand{\thescheme}{\@arabic\c@scheme}%
\newenvironment{scheme}%
               {\@float{scheme}}%
               {\end@float}
\newenvironment{scheme*}%
               {\@dblfloat{scheme}}%
               {\end@dblfloat}
%
%% Macros for display equations %%

\let\SplitEq\relax%

\setlength\arraycolsep{2\@p@t}%
\setlength\tabcolsep{6\@p@t}%
\setlength\arrayrulewidth{.4\@p@t}%
\setlength\doublerulesep{4\@p@t}%
\setlength\tabbingsep{\labelsep}%

\def\eqnarray{%%\initialset%%%introduced on 31Oct15%
   \stepcounter{equation}%
   \def\@currentlabel{\p@equation\theequation}%
   \global\@eqnswtrue
   \m@th
   \global\@eqcnt\z@
   \tabskip\@centering%
   \let\\\@eqncr
   $$\everycr{}\halign to\displaywidth\bgroup
       \hskip\@centering$\displaystyle\tabskip\z@skip{##}$\@eqnsel% for centered equations
%%     \hskip\z@skip\hfil$\displaystyle\tabskip\z@skip{##}$\@eqnsel% for left aligned equations
      &\global\@eqcnt\@ne\hskip \tw@\arraycolsep \hfil${##}$\hfil
      &\global\@eqcnt\tw@ \hskip \tw@\arraycolsep
         $\displaystyle{##}$\hfil\tabskip\@centering
      &\global\@eqcnt\thr@@ \hb@xt@\z@\bgroup\hss##\egroup
       \tabskip\z@skip
      \cr
}
\def\endeqnarray{%
      \@@eqncr
      \egroup
      \global\advance\c@equation\m@ne
   $$\@ignoretrue
}
%

%

%% Macros for Tables %%

\RequirePackage{booktabs}%

\usepackage{etoolbox}
\AtBeginEnvironment{tabular*}{\fontsize{8}{10}\selectfont}
\AtBeginEnvironment{tabular}{\fontsize{8}{10}\selectfont}
\AtBeginEnvironment{tablenotes}{\fontsize{7}{9}\selectfont}



%%%%table notes%%%%
\newenvironment{tablenotes}{\list{}{\setlength{\labelsep}{0\@p@t}%
\setlength{\labelwidth}{0\@p@t}%
\setlength{\leftmargin}{0\@p@t}%
\setlength{\rightmargin}{0\@p@t}%
\setlength{\topsep}{2\@p@t}%
\setlength{\itemsep}{0\@p@t}%
\setlength{\partopsep}{0\@p@t}%
\setlength{\listparindent}{0em}%
\setlength{\parsep}{0\@p@t}}%
\item\relax%
}{\endlist}%

\def\tnote#1{#1}%%

%

%
%\renewenvironment{sidewaystable*}
%               {\@rotdblfloat{table}}
%               {\thisfloatpagestyle{headings}\end@rotdblfloat}%

%\renewenvironment{sidewaysfigure*}
%               {\@rotdblfloat{figure}}
%               {\thisfloatpagestyle{headings}\end@rotdblfloat}

%% Macros for Quote %%

\renewenvironment{quote}{\list{}{\setlength{\labelsep}{0\@p@t}%
\setlength{\labelwidth}{0\@p@t}%
\setlength{\leftmargin}{\parindentvalue}%
\setlength{\rightmargin}{\parindentvalue}%
\setlength{\topsep}{12.5\@p@t \@plus2\@p@t \@minus1\@p@t}%
\setlength{\itemsep}{0\@p@t}%
\setlength{\partopsep}{0\@p@t}%
\setlength{\listparindent}{0em}%
\setlength{\parsep}{0\@p@t}}%
\def\rightline##1{\undefined\vspace*{6\@p@t}\@@line{\hss##1}}%
\item\relax%
}{\end list}%

%%
%%%% Enunciations: Theorem-like styles %%
%%

\let\iint\undefined%
\let\iiint\undefined%
\let\iiiint\undefined%
\let\dddot\undefined%
\let\ddddot\undefined%
\let\overleftrightarrow\undefined%
\let\underrightarrow\undefined%
\let\underleftarrow\undefined%
\let\underleftrightarrow\undefined%

%\RequirePackage[intlimits,sumlimits,namelimits,centertags]{amsmath}%

\RequirePackage{amsmath,amsthm,boites,amssymb,amsbsy}

\newif\ifeproof%

\def\thm@space@setup{%
  \thm@preskip=7\@p@t \thm@postskip=7\@p@t
}
\thm@headpunct{}

\def\thmsep{9\@p@t}%

\newif\ifOldenunciation%
\newif\ifEnunciation%

\newtheoremstyle{plain}% Numbered
{8\@p@t}% Space above
{8\@p@t}% Space below
{\normalsize\it}% Body font
{}% Indent amount
{\itshape}% Theorem head font
{}% Punctuation after theorem head
{\newline}% Space after theorem headi
{}% Theorem head spec (can be left empty, meaning `normal')

\newtheoremstyle{WBstyleone}
  {6\@p@t} % Space above
  {6\@p@t} % Space below
  {\normalfont\it} % Body font
  {\z@} % Indent amount
  {\bfseries} % Theorem head font
  {.} % Punctuation after theorem head
  {5\@p@t} % Space after theorem head
  {} % Theorem head spec (can be left empty, meaning `normal')

\newtheoremstyle{WBstyletwo}
  {6\@p@t} % Space above
  {6\@p@t} % Space below
  {\normalfont} % Body font
  {\z@} % Indent amount
  {\itshape} % Theorem head font
  {.} % Punctuation after theorem head
  {5\@p@t} % Space after theorem head
  {\thmname{#1}\thmnumber{ #2}\thmnote{. #3}} % Theorem head spec (can be left empty, meaning `normal')

\newtheoremstyle{WBstylethree}
  {6\@p@t} % Space above
  {6\@p@t} % Space below
  {\normalfont} % Body font
  {\z@} % Indent amount
  {\bfseries} % Theorem head font
  {.} % Punctuation after theorem head
  {5\@p@t} % Space after theorem head
  {} % Theorem head spec (can be left empty, meaning `normal')

\theoremstyle{WBstyleone}
\newtheorem{theorem}{Theorem}
\newtheorem{construction}{Construction}
\newtheorem{estimate}{Estimate}
\newtheorem{lemma}{Lemma}
\newtheorem{corollary}{Corollary}
\newtheorem{result}{Result}
\newtheorem{algth}{Algorithm}
\newtheorem{proposition}{Proposition}
\newtheorem{hypothesis}{Hypothesis}
\newtheorem{experiment}{Experiment}
\newtheorem{LWrule}{Rule}
\newtheorem{procedure}{Procedure}

\newtheorem{assertion}{Assertion}
\newtheorem{axiom}{Axiom}
\newtheorem{conjecture}{Conjecture}
\newtheorem{criterion}{Criterion}
\newtheorem{reduction}{Reduction}
\newtheorem{sublemma}{Sublemma}


\theoremstyle{WBstyletwo}
\newtheorem{case}{Case}
\newtheorem{remark}{Remark}
\newtheorem{notation}{Notation}

\newtheorem{answer}{Answer}
\newtheorem{base}{Base}
\newtheorem{claim}{Claim}
\newtheorem{ccomment}{Comment}
\newtheorem{subproof}{Subproof}
\newtheorem{conclusion}{Conclusion}
\newtheorem{observation}{Observation}
\newtheorem{subcase}{Subcase}
\newtheorem{step}{Step}
\newtheorem{summary}{Summary}



\theoremstyle{WBstylethree}
\newtheorem{definition}{Definition}
\newtheorem{condition}{Condition}
\newtheorem{property}{Property}
\newtheorem{problem}{Problem}
\newtheorem{fact}{Fact}
\newtheorem{assumption}{Assumption}
\newtheorem{eexample}{Example}
\newtheorem{model}{Model}

\newtheorem{affirmation}{Affirmation}
\newtheorem{application}{Application}
\newtheorem{convention}{Convention}
\newtheorem{discussion}{Discussion}
\newtheorem{exercise}{Exercise}
\newtheorem{question}{Question}
\newtheorem{scholium}{Scholium}
\newtheorem{terminology}{Terminology}

%
%% Macros for Appendix %%
%
\newif\ifappendixsec%

\newcounter{printcitcnt}


%%%%%%%%%%%%%%%%%%%%appendix%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
\newif\if@chapter@pp\@chapter@ppfalse
\newif\if@knownclass@pp\@knownclass@ppfalse
\@ifundefined{chapter}{%
  \@ifundefined{section}{}{\@knownclass@pptrue}}{%
  \@chapter@pptrue\@knownclass@pptrue}
\providecommand{\phantomsection}{}
\newcounter{@pps}
  \renewcommand{\the@pps}{\alph{@pps}}
\newif\if@pphyper
  \@pphyperfalse
\AtBeginDocument{%
  \@ifpackageloaded{hyperref}{\@pphypertrue}{}}

\newif\if@dotoc@pp\@dotoc@ppfalse
\newif\if@dotitle@pp\@dotitle@ppfalse
\newif\if@dotitletoc@pp\@dotitletoc@ppfalse
\newif\if@dohead@pp\@dohead@ppfalse
\newif\if@dopage@pp\@dopage@ppfalse
\DeclareOption{toc}{\@dotoc@pptrue}
\DeclareOption{title}{\@dotitle@pptrue}
\DeclareOption{titletoc}{\@dotitletoc@pptrue}
\DeclareOption{header}{\@dohead@pptrue}
\DeclareOption{page}{\@dopage@pptrue}
\ProcessOptions\protect\relax
\newcommand{\@ppendinput}{}
\if@knownclass@pp\else
  \PackageWarningNoLine{appendix}%
    {There is no \protect\chapter\space or \protect\section\space command.\MessageBreak
     The appendix package will not be used}
  \renewcommand{\@ppendinput}{\endinput}
\fi
\@ppendinput

\newcommand{\appendixtocon}{\@dotoc@pptrue}
\newcommand{\appendixtocoff}{\@dotoc@ppfalse}
\newcommand{\appendixpageon}{\@dopage@pptrue}
\newcommand{\appendixpageoff}{\@dopage@ppfalse}
\newcommand{\appendixtitleon}{\@dotitle@pptrue}
\newcommand{\appendixtitleoff}{\@dotitle@ppfalse}
\newcommand{\appendixtitletocon}{\@dotitletoc@pptrue}
\newcommand{\appendixtitletocoff}{\@dotitletoc@ppfalse}
\newcommand{\appendixheaderon}{\@dohead@pptrue}
\newcommand{\appendixheaderoff}{\@dohead@ppfalse}
\newcounter{@ppsavesec}
\newcounter{@ppsaveapp}
\setcounter{@ppsaveapp}{0}
\newcommand{\@ppsavesec}{%
  \if@chapter@pp \setcounter{@ppsavesec}{\value{chapter}} \else
                 \setcounter{@ppsavesec}{\value{section}} \fi}
\newcommand{\@pprestoresec}{%
  \if@chapter@pp \setcounter{chapter}{\value{@ppsavesec}} \else
                 \setcounter{section}{\value{@ppsavesec}} \fi}
\newcommand{\@ppsaveapp}{%
  \if@chapter@pp \setcounter{@ppsaveapp}{\value{chapter}} \else
                 \setcounter{@ppsaveapp}{\value{section}} \fi}
\newcommand{\restoreapp}{%
  \if@chapter@pp \setcounter{chapter}{\value{@ppsaveapp}} \else
                 \setcounter{section}{\value{@ppsaveapp}} \fi}
\providecommand{\appendixname}{Appendix}
\newcommand{\appendixtocname}{Appendices}
\newcommand{\appendixpagename}{Appendices}
\newcommand{\appendixpage}{%
  \if@chapter@pp \@chap@pppage \else \@sec@pppage \fi
}
\newcommand{\clear@ppage}{%
  \if@openright\cleardoublepage\else\clearpage\fi}



\newcommand{\@sec@pppage}{%
  \par
  \addvspace{4ex}%
  \@afterindentfalse
  {\parindent \z@ \raggedright
   \interlinepenalty \@M
   \normalfont
    \bfseries \appendixpagename%
   \markboth{}{}\par}%
  \if@dotoc@pp
    \addappheadtotoc
  \fi
  \nobreak
  \vskip 3ex
  \@afterheading
}

\newif\if@pptocpage
  \@pptocpagetrue
\newcommand{\noappendicestocpagenum}{\@pptocpagefalse}
\newcommand{\appendicestocpagenum}{\@pptocpagetrue}
\newcommand{\addappheadtotoc}{%
  \phantomsection
  \if@chapter@pp
    \if@pptocpage
      \addcontentsline{toc}{chapter}{\appendixtocname}%
    \else
      \if@pphyper
        \addtocontents{toc}%
          {\protect\contentsline{chapter}{\appendixtocname}{}{\@currentHref}}%
      \else
        \addtocontents{toc}%
          {\protect\contentsline{chapter}{\appendixtocname}{}}%
      \fi
    \fi
  \else
    \if@pptocpage
      \addcontentsline{toc}{section}{\appendixtocname}%
    \else
      \if@pphyper
        \addtocontents{toc}%
          {\protect\contentsline{section}{\appendixtocname}{}{\@currentHref}}%
      \else
        \addtocontents{toc}%
          {\protect\contentsline{section}{\appendixtocname}{}}%
      \fi
    \fi
  \fi
}

\providecommand{\theH@pps}{\alph{@pps}}

\newcommand{\@resets@pp}{\par
  \@ppsavesec
  \stepcounter{@pps}
  \setcounter{section}{0}%
  \if@chapter@pp
    \setcounter{chapter}{0}%
    \renewcommand\@chapapp{\appendixname}%
    \renewcommand\thechapter{\@Alph\c@chapter}%
  \else
    \setcounter{section}{0}%
\ifnum\c@secnumdepth=1
    \renewcommand\thesection{\appendixname\ \@Alph\c@section}%Appendix
\else
  \renewcommand\thesection{\@Alph\c@section}%
\fi
  \fi
  \if@pphyper
    \if@chapter@pp
      \renewcommand{\theHchapter}{\theH@pps.\Alph{chapter}}%
    \else
      \renewcommand{\theHsection}{\theH@pps.\Alph{section}}%
    \fi
    \def\Hy@chapapp{\appendixname}%
  \fi
  \restoreapp
}

\renewenvironment{appendix}{\bmsection*{APPENDIX}\vskip12\@p@t\global\appendixsectrue%
  \setcounter{figure}{0}%
  \setcounter{table}{0}%
  \setcounter{equation}{0}%
  \@resets@pp
  \if@dotoc@pp
    \if@dopage@pp              % both page and toc
      \if@chapter@pp           % chapters
        \clear@ppage
      \fi
      \appendixpage
    \else                      % toc only
       \if@chapter@pp          % chapters
         \clear@ppage
       \fi
      \addappheadtotoc
    \fi
  \else
    \if@dopage@pp              % page only
      \appendixpage
    \fi
  \fi
  \if@chapter@pp
    \if@dotitletoc@pp \@redotocentry@pp{chapter} \fi
  \else
    \if@dotitletoc@pp \@redotocentry@pp{section} \fi
    \if@dohead@pp
      \def\sectionmark##1{%
        \if@twoside
          \markboth{\@formatsecmark@pp{##1}}{}
        \else
          \markright{\@formatsecmark@pp{##1}}{}
        \fi}
    \fi
    \if@dotitle@pp
      \def\sectionname{\appendixname}
      \def\@seccntformat##1{\@ifundefined{##1name}{}{\csname ##1name\endcsname\ }%
        \csname the##1\endcsname\quad}
    \fi
  \fi%
  \renewcommand\thefigure{\@Alph\c@section\arabic{figure}}%
  \renewcommand\thetable{\@Alph\c@section\arabic{table}}%
  \renewcommand\theequation{\@Alph\c@section\arabic{equation}}%
}{%
  \@ppsaveapp\@pprestoresec}

\newcommand{\setthesection}{\thechapter.\Alph{section}}
\newcommand{\setthesubsection}{\thesection.\Alph{subsection}}

\newcommand{\@resets@ppsub}{\par
  \stepcounter{@pps}
  \if@chapter@pp
    \setcounter{section}{0}
    \renewcommand{\thesection}{\setthesection}
  \else
    \setcounter{subsection}{0}
    \renewcommand{\thesubsection}{\setthesubsection}
  \fi
  \if@pphyper
    \if@chapter@pp
      \renewcommand{\theHsection}{\theH@pps.\setthesection}%
    \else
      \renewcommand{\theHsubsection}{\theH@pps.\setthesubsection}%
    \fi
    \def\Hy@chapapp{\appendixname}%
  \fi
}

\newenvironment{subappendices}{%
  \@resets@ppsub
  \if@chapter@pp
    \if@dotitletoc@pp \@redotocentry@pp{section} \fi
    \if@dotitle@pp
      \def\sectionname{\appendixname}
      \def\@seccntformat##1{\@ifundefined{##1name}{}{\csname ##1name\endcsname\ }%
        \csname the##1\endcsname\quad}
    \fi
  \else
    \if@dotitletoc@pp \@redotocentry@pp{subsection} \fi
    \if@dotitle@pp
      \def\subsectionname{\appendixname}
      \def\@seccntformat##1{\@ifundefined{##1name}{}{\csname ##1name\endcsname\ }%
        \csname the##1\endcsname\quad}
    \fi
  \fi}{}

\newcommand{\@formatsecmark@pp}[1]{%
  \MakeUppercase{\appendixname\space
    \ifnum \c@secnumdepth >\z@
      \thesection\quad
    \fi
    #1}}
\newcommand{\@redotocentry@pp}[1]{%
  \let\oldacl@pp=\addcontentsline
  \def\addcontentsline##1##2##3{%
    \def\@pptempa{##1}\def\@pptempb{toc}%
    \ifx\@pptempa\@pptempb
      \def\@pptempa{##2}\def\@pptempb{#1}%
      \ifx\@pptempa\@pptempb
\oldacl@pp{##1}{##2}{\appendixname\space ##3}%
      \else
        \oldacl@pp{##1}{##2}{##3}%
      \fi
    \else
      \oldacl@pp{##1}{##2}{##3}%
    \fi}
}
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

% Footnotes
%
%\renewcommand\thefootnote{\@fnsymbol\c@footnote}%


\renewcommand\footnoterule{%%LN
  \kern-3\@p@t%
  \hrule height0.5\@p@t%
  \kern4.4\@p@t%
}%
\renewcommand\@makefntext[1]{%
 %   \parindent \z@%
    \noindent
    \hbox{\hss\@makefnmark}\,#1}

%%%%%%%%%%%%%%%%%%%%%%  Enumerate and Bullet list

%%% LISTS %%

\newdimen\labelwidthi
\newdimen\labelwidthii
\newdimen\labelwidthiii
\newdimen\labelwidthiv
\def\normal@labelsep{.4em}
\labelsep\normal@labelsep
\settowidth{\labelwidthi}{(iii)}
\settowidth{\labelwidthii}{(d)}
\settowidth{\labelwidthiii}{(iii)}
\settowidth{\labelwidthiv}{(M)}
\leftmargini\labelwidthi    \advance\leftmargini\labelsep
\leftmarginii\labelwidthii  \advance\leftmarginii\labelsep
\leftmarginiii\labelwidthiii \advance\leftmarginiii\labelsep
\leftmarginiv\labelwidthiv  \advance\leftmarginiv\labelsep
\def\setleftmargin#1#2{\settowidth{\@tempdima}{#2}\labelsep\normal@labelsep
  \csname labelwidth#1\endcsname\@tempdima
  \@tempdimb\@tempdima \advance\@tempdimb\labelsep
  \csname leftmargin#1\endcsname\@tempdimb}
\def\@listI{\leftmargin\leftmargini
  \labelwidth\labelwidthi \labelsep\normal@labelsep
  \topsep1\@p@t  \partopsep\z@ \parsep\z@ \itemsep\z@
  \listparindent 11em}
\def\@listii{\leftmargin\leftmarginii
  \labelwidth\labelwidthii \labelsep\normal@labelsep
  \topsep\z@ \partopsep\z@ \parsep\z@ \itemsep\z@
  \listparindent 1em}
\def\@listiii{\leftmargin\leftmarginiii
  \labelwidth\labelwidthiii \labelsep\normal@labelsep
  \topsep\z@ \partopsep\z@ \parsep\z@ \itemsep\z@
  \listparindent 1em}
\def\@listiv{\leftmargin\leftmarginiv
  \labelwidth\labelwidthiv \labelsep\normal@labelsep
  \topsep\z@ \partopsep\z@ \parsep\z@ \itemsep\z@
  \listparindent 1em}
\let\@listi\@listI
\@listi
\def\left@label#1{{#1}\hss}
\def\right@label#1{\hss\llap{#1}}
\def\thick@label#1{\hspace\labelsep #1}
\newcount\@maxlistdepth
\@maxlistdepth=4
%
\setlength  \labelsep  {.5em}

\setlength  \labelwidth{\leftmargini}
\addtolength\labelwidth{-\labelsep}

\@beginparpenalty -\@lowpenalty
\@endparpenalty   -\@lowpenalty
\@itempenalty     -\@lowpenalty
\def\labelitemi{\hspace{9pt}\fontsize{10}{11}\selectfont\raise.3\@p@t\hbox{$\bullet$}}
\def\labelitemii{$\cdot$}
\def\labelenumi{\theenumi}        \def\theenumi{\arabic{enumi}.}
\def\labelenumii{(\alph{enumii})}   \def\theenumii{\theenumi\alph{enumii}}
\def\labelenumiii{(\roman{enumiii})}\def\theenumiii{\theenumii.\roman{enumiii}}
\def\labelenumiv{(\Alph{enumiv})}   \def\theenumiv{\theenumiii.\Alph{enumiv}}

%\def\enumerate{%
%  \ifnum \@enumdepth >\@maxlistdepth
%    \@toodeep
%  \else
%    \advance\@enumdepth \@ne
%    \edef\@enumctr{enum\romannumeral\the\@enumdepth}%
%    \list{\csname label\@enumctr\endcsname}%
%       {\usecounter{\@enumctr}
%       {\let\makelabel=\right@label}
%       \topsep6\@p@t
%      \itemsep3\@p@t %%% for spaced list
%	\leftmargin15\@p@t}
	%\leftmargin19.8\@p@t}
  %\fi}

\def\itemize{%
  \ifnum \@itemdepth >\@maxlistdepth
    \@toodeep
  \else
    \advance\@itemdepth \@ne
    \edef\@itemitem{labelitem\romannumeral\the\@itemdepth}%
     \setleftmargin{i}{--}%
     \setleftmargin{ii}{$\cdot$}%
    \list{\csname\@itemitem\endcsname}%
       {\let\makelabel\right@label\topsep12\@p@t}
     \setlength  \labelsep  {1em}\setlength\itemindent{0pt}
     \leftskip12\@p@t
      \itemsep4\@p@t
         \fi}
\def\verse{\let\\=\@centercr
  \list{}{\itemsep\z@
  \itemindent \z@
  \listparindent\z@
  \rightmargin 0\@p@t
  \leftmargin \rightmargin}\item[]}
\let\endverse\endlist
\def\quotation{\list{}{\itemindent\z@
 \leftmargin 1em \rightmargin 0\@p@t%\z@
  \parsep \z@ \@plus 1\@p@t}\item[]}
\let\endquotation=\endlist
%\def\quote{\list{}{\fontsize{9}{11}\selectfont\itemindent\z@
%   \leftmargin 1pc \rightmargin 0\@p@t \topsep1\@p@t\rightmargin0em}%
%\item[]}
%\let\endquote=\endlist
%\def\descriptionlabel#1{\hspace\labelsep \itshape #1}
%\def\description{\list{}{\labelwidth\z@
%  \leftmargin10\@p@t %\z@
%\itemindent-16\@p@t %\leftmargin
%\topsep5\@p@t
%  \let\makelabel\descriptionlabel}}
%\let\enddescription\endlist

%%%%%%%%%%%%%%%


%
%%% Macros for float algorithm %%%

%\usepackage[plain]{algorithm} %% wrapper
%\usepackage{varwidth}%
%\usepackage{algorithmicx}%
%\usepackage{algpseudocode}%
\usepackage{algorithm, algorithmicx, algpseudocode}

\usepackage{listings}%
\usepackage{alltt}%


%%% Macros for upright lowercase greek characters %%%


%% since small triangle symbol is not found used \triangleright and rotated to 90 degrees %%


\def\@jissue{}%
\def\jissue#1{\gdef\@jissue{#1}}%

\def\@licenseVal{}%
\def\licenseVal#1{\gdef\@licenseVal{#1}}%

\def\@supplinks{}%
\def\supplinks#1{\gdef\@supplinks{#1}}%

\def\@coninterest{}%
\def\coninterest#1{\gdef\@coninterest{#1}}%

\def\@confinfo{}%
\def\confinfo#1{\def\@confinfo{#1}}%

\def\@cdate{}%
\def\cdate#1{\gdef\@cdate{#1}}%

\def\@cname{}%
\def\cname#1{\gdef\@cname{#1}}%

\def\@cloc{}%
\def\cloc#1{\gdef\@cloc{#1}}%

\def\tabheadfont{}%

\let\fullpagewidth\textwidth%

\def\jargon#1{#1}%

\newenvironment{bm}[1][]{\begingroup%
\if!#1!\else\section*{#1}\fi%
}{\endgroup}

\let\glosslist\description%
\let\endglosslist\enddescription%

\newenvironment{ack}[1][]{\begingroup%
\if!#1!\else\section*{#1}\fi%
}{\endgroup}

\def\APLrightarrowbox{}% Use \usepackage{wasysym} to get this symbol
\def\boxcircle{}%

%% Macros for \citation tag %%

\newbox\jnlcitationbox%

\def\cname#1{\gdef\author##1{##1}#1}%
\def\cyear#1{#1}%
\def\ctitle#1{#1}%
\def\cjournal#1{{#1}}%
\def\cvol#1{{#1}}%
\def\subtitle#1{\gdef\@subtitle{#1}}


\gdef\@jnlcitation{}%
\def\jnlcitation#1{\gdef\@jnlcitation{\textbf{How to cite this article: }#1}}%

\newdimen\jnlcithsize%
%\jnlcithsize=\hsize%
\advance\jnlcithsize-0\@p@t% fbox sep x 2
\advance\jnlcithsize-1\@p@t% fbox rule x 2

\gdef\printjnlcitation{\removelastskip\vskip18\@p@t\fboxsep=4\@p@t\fboxrule=0.5\@p@t%
\hskip-\parindentvalue\fbox{\vbox{\noindent\@jnlcitation}}}%

%\AtEndDocument{\ifappendixsec\else\printjnlcitation\fi}%


%% Misc math macros %%

\newcommand{\xleftrightarrow}[2][]{\ext@arrow 3399\leftrightarrowfill@{#1}{#2}}%

%
%%% Macros for Biography %%%
%
\RequirePackage{wrapfig}%
%
%  \begin{wrapfigure}[12]{r}[34\@p@t]{5cm} <figure> \end{wrapfigure}
%                     --  -  ----  ---
%  [number of narrow lines] {placement} [overhang] {width of figure}
\newcount\wraplines%
\wraplines=5%
%
\newbox\@authorfigbox%
\newskip\@authorfigboxdim%
%

\newskip\biofigadjskip%
\biofigadjskip=0\@p@t%
%
\newenvironment{biography}[2]{\vskip10\@p@t
\begin{breakbox}
%\addvspace{10.5\@p@t plus3.375\@p@t minus1.6875\@p@t}%\lineno@off%par
\def\author##1{\textbf{##1}}%
\def\@authorfig{{#1}}%
\setbox\@authorfigbox=\hbox{#1}%%
\@authorfigboxdim=\wd\@authorfigbox%
\advance\@authorfigboxdim by 10\@p@t
\if@dblspace\else\wraplines=7\fi%
%14\@p@t%
\noindent{%
\ifx\@authorfig\@empty\else\unskip%
\begin{wrapfigure}[\wraplines]{l}[0\@p@t]{\@authorfigboxdim}%
\vskip-25\@p@t\vskip\biofigadjskip%
\@authorfig%
\end{wrapfigure}%
\fi%
#2%
\par%
}\vspace*{40\@p@t}\end{breakbox}}{\par\addvspace{10.5\@p@t plus3.375\@p@t minus1.6875\@p@t}}

\def\emph#1{\textit{#1}}

\setlength{\parindent}{0pt}


%%%%% Babel style do not use
%%%\AtBeginDocument{\@ifpackageloaded{babel}{%
%%%\Error - Please Do not use Babel.sty. The babel style is included in class file}{}%
%%%}%

\algrenewcommand\ALG@beginalgorithmic{\ttfamily}
\usepackage{afterpage}
\AddToHook{shipout/after}{%\thispagestyle{spectrumPGTMlast}%
  \ifnum \c@totalpages=1\relax
    \newgeometry{bottom=18mm,top=12mm}%
    \global\textheight=\textheight
    \global\topmargin=\topmargin
    %\global\@colht=\textheight % maybe not needed
    %\global\@colroom=\textheight % maybe not needed
    %\global\vsize=\textheight % maybe not needed
  \fi
}


\endinput
