CREATE TABLE v1(id int, embedding vector(3));
INSERT INTO v1 VALUES(1, STRING_TO_VECTOR('[1,2,3]'));
INSERT INTO v1 VALUES(2, STRING_TO_VECTOR('[4,5,6]'));
INSERT INTO v1 VALUES(3, STRING_TO_VECTOR('[1.5,-2,3.25]'));

CREATE VECTOR INDEX idx1 ON v1(embedding) WITH (lists=10, probes=2);
CREATE VECTOR INDEX idx_bad ON v1(embedding) WITH (lists=0, probes=2);

EXIT;
